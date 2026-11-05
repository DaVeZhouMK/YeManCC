import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fingerprintCase, inspectCachedResult, deduplicateCases } from './identity-cache.mjs';

// Only synthetic files here. Never os.tmpdir(), the product, or a formal fixture.
const EVIDENCE = path.resolve('G:/YeManCC-Work/Testing/YMCC/Evidence/FixtureCostOptimization-20261023/cache-engine');
fs.mkdirSync(EVIDENCE, { recursive: true });
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const pin = file => ({ path: file, sha256: hash(file) });
const codes = value => value.reasons.map(reason => reason.code);

function fixture({ noCandidates = false } = {}) {
  const root = fs.mkdtempSync(path.join(EVIDENCE, 'synthetic-'));
  const repoRoot = path.join(root, 'repo');
  const caseDir = path.join(root, 'original-case');
  const external = path.join(root, 'external');
  for (const dir of [repoRoot, caseDir, external]) fs.mkdirSync(dir);
  for (const [file, contents] of Object.entries({ 'entry.mjs': 'export const entry = 1;\n', 'source.ts': 'export const source = 1;\n',
    'dep.mjs': 'export const dependency = 1;\n', 'reference.json': '{"expected":1}\n' })) fs.writeFileSync(path.join(repoRoot, file), contents);
  const tool = path.join(external, 'compiler.bin'), artifact = path.join(external, 'candidate.bin');
  fs.writeFileSync(tool, 'synthetic tool bytes');
  fs.writeFileSync(artifact, 'synthetic candidate bytes');
  const adapter = { id: 'synthetic.one', title: 'Synthetic only', path: 'entry.mjs', kind: 'node', args: ['--synthetic', '{outDir}'],
    timeoutMs: 1000, scope: 'source-contract', profiles: ['desktop'], evidenceInputs: ['source.ts'], referenceInputs: ['reference.json'],
    dependencyInputs: ['dep.mjs'], resultFiles: ['details.json'], ...(noCandidates ? {} : { artifactInputs: [artifact] }) };
  const options = { closureComplete: true, resolvedInputs: ['dep.mjs'], toolClosureComplete: true };
  const toolIdentities = noCandidates ? [] : [tool];
  const fingerprint = (changes = {}) => fingerprintCase({ repoRoot, adapter, toolIdentities, options, ...changes });
  const identity = fingerprint();
  assert.equal(identity.cacheable, true, JSON.stringify(identity.reasons));
  const stdout = path.join(caseDir, 'stdout.log'), stderr = path.join(caseDir, 'stderr.log'), details = path.join(caseDir, 'details.json');
  fs.writeFileSync(stdout, 'synthetic execution receipt\n');
  fs.writeFileSync(stderr, '');
  fs.writeFileSync(details, '{"status":"PASS"}\n');
  const sourceRow = name => ({ path: name, sha256: hash(path.join(repoRoot, name)), status: 'OK' });
  const raw = { id: adapter.id, source: adapter.path, scope: adapter.scope, status: 'PASS', sourceChanged: false, exitCode: 0,
    sourceInputs: ['entry.mjs', 'source.ts'].map(sourceRow), referenceInputs: ['reference.json'].map(sourceRow),
    dependencyInputs: ['dep.mjs'].map(sourceRow), structuredEvidence: [{ ...pin(details), failed: false }] };
  const outputFiles = [stdout, stderr, details];
  if (!noCandidates) {
    const bundle = path.join(caseDir, 'entry.mjs'), sourceMap = path.join(caseDir, 'entry.mjs.map');
    fs.writeFileSync(bundle, '// synthetic recorded bundle\n');
    fs.writeFileSync(sourceMap, '{}\n');
    raw.candidateArtifactChanged = false;
    raw.toolDependencyInputs = [{ ...pin(tool), status: 'OK' }];
    raw.candidateArtifacts = [{ ...pin(artifact), status: 'OK' }, { ...pin(bundle), status: 'OK' }];
    raw.bundle = pin(bundle);
    raw.bundledOutputs = [pin(bundle), pin(sourceMap)];
    outputFiles.push(bundle, sourceMap);
  }
  const resultPath = path.join(caseDir, 'result.json');
  const entry = { identity: identity.identity, result: null, evidencePins: [] };
  const save = () => {
    fs.writeFileSync(resultPath, JSON.stringify(raw, null, 2));
    entry.result = pin(resultPath);
    entry.evidencePins = [resultPath, ...outputFiles].map(pin);
  };
  save();
  return { root, repoRoot, caseDir, external, adapter, options, toolIdentities, tool, artifact, raw, entry, identity, fingerprint,
    resultPath, stdout, stderr, details, outputFiles, save, inspect: () => inspectCachedResult({ identity, cacheEntry: entry }) };
}

test('stable key covers absolute real paths, bytes and the actual Node runtime', () => {
  const f = fixture();
  const a = f.fingerprint();
  const b = f.fingerprint({ adapter: Object.fromEntries(Object.entries(f.adapter).reverse()), options: Object.fromEntries(Object.entries(f.options).reverse()) });
  assert.equal(a.identity, b.identity);
  assert.deepEqual(a.manifest.runtime, { node: process.version, platform: process.platform, arch: process.arch });
  assert.ok(a.manifest.tools.some(row => row.path === process.execPath && /^[a-f0-9]{64}$/.test(row.sha256)));
  assert.ok([...a.manifest.inputs, ...a.manifest.tools].every(row => path.isAbsolute(row.path) && path.isAbsolute(row.realPath)));
  assert.equal(/mtime|gitHead/i.test(JSON.stringify(a.manifest)), false);
});

test('tool, source, reference, artifact and resolved dependency byte changes invalidate only their case', () => {
  const f = fixture();
  const other = fixture();
  for (const file of [f.tool, f.artifact, ...['entry.mjs', 'source.ts', 'reference.json', 'dep.mjs'].map(name => path.join(f.repoRoot, name))]) {
    const original = fs.readFileSync(file);
    fs.appendFileSync(file, '\nchanged bytes');
    assert.notEqual(f.fingerprint().identity, f.identity.identity, file);
    fs.writeFileSync(file, original);
  }
  assert.equal(other.fingerprint().identity, other.identity.identity);
});

test('identical mtime does not hide changed bytes; touching mtime alone preserves identity', () => {
  const f = fixture(), file = path.join(f.repoRoot, 'source.ts');
  const before = fs.statSync(file);
  fs.utimesSync(file, new Date(1_000_000), new Date(1_000_000));
  assert.equal(f.fingerprint().identity, f.identity.identity);
  fs.writeFileSync(file, 'export const source = 2;\n');
  fs.utimesSync(file, before.atime, before.mtime);
  assert.notEqual(f.fingerprint().identity, f.identity.identity);
});

test('args/kind/timeout/scope/completion/candidate/unknown config and options are identity inputs', () => {
  const f = fixture();
  for (const patch of [{ args: ['--other'] }, { kind: 'typescript' }, { timeoutMs: 2000 }, { scope: 'host-mock' },
    { completionContract: { version: 1, tokens: ['DONE'] } }, { candidate: { channel: 'other' } }, { futureRunnerOption: { enabled: true } }]) {
    assert.notEqual(f.fingerprint({ adapter: { ...f.adapter, ...patch } }).identity, f.identity.identity);
  }
  assert.notEqual(f.fingerprint({ options: { ...f.options, payloadRoot: '/different/payload' } }).identity, f.identity.identity);
  assert.equal(f.fingerprint({ adapter: { ...f.adapter, id: 'other.id', title: 'Other title' } }).identity, f.identity.identity);
});

test('unreviewed or missing closures, unresolved rows, stale declared SHA and non-JSON config fail closed', () => {
  const f = fixture();
  for (const options of [{}, { ...f.options, closureComplete: false }, { ...f.options, resolvedInputs: undefined }, { ...f.options, toolClosureComplete: false }]) {
    const identity = f.fingerprint({ options });
    assert.equal(identity.cacheable, false);
    assert.equal(identity.identity, null);
    assert.equal(inspectCachedResult({ identity, cacheEntry: f.entry }).status, 'UNCACHEABLE');
  }
  const unresolved = f.fingerprint({ adapter: { ...f.adapter, dependencyInputs: [{ path: 'dep.mjs', status: 'UNRESOLVED' }] } });
  assert.ok(codes(unresolved).includes('INPUT_NOT_RESOLVED'));
  const stale = f.fingerprint({ options: { ...f.options, resolvedInputs: [{ path: 'dep.mjs', sha256: '0'.repeat(64) }] } });
  assert.ok(codes(stale).includes('INPUT_SHA256_MISMATCH'));
  assert.equal(f.fingerprint({ adapter: { ...f.adapter, callback: () => true } }).cacheable, false);
});

test('declared dependency pins and absolute resolved paths inside repo are re-read; external resolved sources are refused', () => {
  const f = fixture(), file = path.join(f.repoRoot, 'dep.mjs');
  assert.equal(f.fingerprint({ options: { ...f.options, resolvedInputs: [{ ...pin(file), status: 'OK' }] } }).cacheable, true);
  assert.equal(f.fingerprint({ options: { ...f.options, resolvedInputs: [f.tool] } }).cacheable, false);
  assert.equal(f.fingerprint({ adapter: { ...f.adapter, dependencyInputs: ['missing.mjs'] } }).cacheable, false);
  assert.equal(f.fingerprint({ adapter: { ...f.adapter, evidenceInputs: ['.'] } }).cacheable, false);
  assert.equal(f.fingerprint({ adapter: { ...f.adapter, referenceInputs: ['../outside.json'] } }).cacheable, false);
});

test('TypeScript/PowerShell cannot claim a complete tool closure with no explicit tools', () => {
  const f = fixture();
  for (const kind of ['typescript', 'powershell']) {
    const identity = f.fingerprint({ adapter: { ...f.adapter, kind }, toolIdentities: [] });
    assert.equal(identity.cacheable, false);
    assert.ok(codes(identity).includes('EXPLICIT_TOOL_REQUIRED'));
  }
  assert.ok(codes(f.fingerprint({ toolIdentities: [{ path: f.tool, sha256: '0'.repeat(64) }] })).includes('INPUT_SHA256_MISMATCH'));
});

test('source and artifact junctions escaping declared boundaries are refused', () => {
  const f = fixture();
  fs.writeFileSync(path.join(f.external, 'escape.mjs'), 'same bytes');
  fs.symlinkSync(f.external, path.join(f.repoRoot, 'escaped'), process.platform === 'win32' ? 'junction' : 'dir');
  assert.ok(codes(f.fingerprint({ adapter: { ...f.adapter, evidenceInputs: ['escaped/escape.mjs'] } })).includes('SYMLINK_OUTSIDE_ROOT'));
  const holder = path.join(f.root, 'holder');
  fs.mkdirSync(holder);
  fs.symlinkSync(f.external, path.join(holder, 'escaped'), process.platform === 'win32' ? 'junction' : 'dir');
  assert.ok(codes(f.fingerprint({ adapter: { ...f.adapter, artifactInputs: [path.join(holder, 'escaped', 'candidate.bin')] } })).includes('SYMLINK_OUTSIDE_ROOT'));
});

test('a verified PASS is CACHED_REFERENCE and contributes zero actual passes or executions', () => {
  const f = fixture(), result = f.inspect();
  assert.deepEqual([result.hit, result.reusable, result.status, result.previousStatus], [true, true, 'CACHED_REFERENCE', 'PASS']);
  assert.deepEqual(result.reasons, []);
  assert.deepEqual(result.reference, f.raw);
  assert.equal(result.executedThisRun, false);
  assert.equal(result.actualPassedThisRun, false);
  assert.equal([result].filter(row => row.status === 'PASS').length, 0);
  assert.equal([result].filter(row => row.actualPassedThisRun).length, 0);
});

test('original Node runner shape without any candidate declaration accepts absent candidate changed flag as N/A', () => {
  const f = fixture({ noCandidates: true });
  assert.equal(Object.hasOwn(f.raw, 'candidateArtifactChanged'), false);
  const bytes = fs.readFileSync(f.resultPath);
  assert.equal(f.inspect().status, 'CACHED_REFERENCE');
  assert.deepEqual(fs.readFileSync(f.resultPath), bytes);
  f.raw.candidateArtifactChanged = true;
  f.save();
  assert.equal(f.inspect().reusable, false);
});

test('same-identity raw failures stay unchanged references and ignore optimistic cache metadata', () => {
  const f = fixture();
  for (const status of ['FAIL', 'UNKNOWN', 'BLOCKED', 'TIMEOUT', 'INCOMPLETE', 'NOT_RUN']) {
    f.raw.status = status;
    f.raw.exitCode = 1;
    f.save();
    f.entry.status = 'PASS';
    f.entry.resultStatus = 'PASS';
    const result = f.inspect();
    assert.deepEqual([result.hit, result.reusable, result.status, result.previousStatus], [false, true, 'UNCHANGED_FAILURE_REFERENCE', status]);
    assert.equal(result.reference.status, status);
    assert.deepEqual([result.executedThisRun, result.actualPassedThisRun], [false, false]);
  }
});

test('different cache key, missing entries and fabricated manifests cannot be reused', () => {
  const f = fixture();
  assert.equal(inspectCachedResult({ identity: f.identity }).reusable, false);
  assert.equal(inspectCachedResult({ identity: f.identity, cacheEntry: { ...f.entry, identity: '0'.repeat(64) } }).reusable, false);
  const forged = structuredClone(f.identity);
  forged.manifest.adapter.args = ['--forged'];
  assert.equal(inspectCachedResult({ identity: forged, cacheEntry: f.entry }).reusable, false);
});

test('stale identity inputs are rehashed at inspection even without recomputing the caller key', () => {
  const f = fixture();
  fs.appendFileSync(path.join(f.repoRoot, 'dep.mjs'), '// drift\n');
  assert.equal(f.inspect().reusable, false);
  assert.equal(inspectCachedResult({ identity: f.fingerprint(), cacheEntry: f.entry }).reusable, false);
});

test('pins must cover the original result, logs, resultFiles and recorded output bundle/map', () => {
  const f = fixture(), all = f.entry.evidencePins;
  assert.equal(inspectCachedResult({ identity: f.identity, cacheEntry: { ...f.entry, evidencePins: undefined } }).reusable, false);
  for (const pinToRemove of all) {
    const entry = { ...f.entry, evidencePins: all.filter(row => row.path !== pinToRemove.path) };
    assert.equal(inspectCachedResult({ identity: f.identity, cacheEntry: entry }).reusable, false, pinToRemove.path);
  }
});

test('forged raw PASS, modified stdout and missing stderr cannot be hidden by index metadata', () => {
  const f = fixture();
  const original = fs.readFileSync(f.resultPath);
  fs.writeFileSync(f.resultPath, JSON.stringify({ ...f.raw, status: 'FAIL' }));
  assert.equal(f.inspect().reusable, false);
  fs.writeFileSync(f.resultPath, original);
  fs.appendFileSync(f.stdout, 'forged receipt');
  assert.equal(f.inspect().reusable, false);
  fs.writeFileSync(f.stdout, 'synthetic execution receipt\n');
  fs.unlinkSync(f.stderr);
  assert.equal(f.inspect().reusable, false);
});

test('candidate/source changed flags and contrary failure/completion evidence reject even a pinned PASS', () => {
  const f = fixture(), original = structuredClone(f.raw);
  for (const patch of [{ sourceChanged: true }, { sourceChanged: undefined }, { candidateArtifactChanged: true }, { candidateArtifactChanged: undefined },
    { candidateArtifactChanged: 'false' }, { exitCode: 1 }, { requiredEvidenceFailed: true }, { timedOut: true }, { completionProof: { status: 'FAIL' } }]) {
    for (const key of Object.keys(f.raw)) delete f.raw[key];
    Object.assign(f.raw, original, patch);
    f.save();
    assert.equal(f.inspect().reusable, false, JSON.stringify(patch));
  }
});

test('raw source, tool, artifact and dependency hashes must be declared and match current identity', () => {
  const f = fixture(), original = structuredClone(f.raw);
  for (const field of ['sourceInputs', 'referenceInputs', 'dependencyInputs', 'toolDependencyInputs', 'candidateArtifacts']) {
    Object.assign(f.raw, structuredClone(original));
    f.raw[field][0].sha256 = '0'.repeat(64);
    f.save();
    assert.equal(f.inspect().reusable, false, field);
  }
  Object.assign(f.raw, original, { sourceInputs: [] });
  f.save();
  assert.equal(f.inspect().reusable, false);
});

test('a declared completion contract requires the original completion proof', () => {
  const f = fixture();
  f.adapter.completionContract = { kind: 'synthetic-token', marker: 'DONE' };
  f.identity = f.fingerprint();
  f.entry.identity = f.identity.identity;
  assert.equal(inspectCachedResult({ identity: f.identity, cacheEntry: f.entry }).reusable, false);
  f.raw.completionProof = { status: 'PASS' };
  f.save();
  assert.equal(inspectCachedResult({ identity: f.identity, cacheEntry: f.entry }).status, 'CACHED_REFERENCE');
});

test('receipt symlink/junction redirection is refused even when replacement bytes match', () => {
  const f = fixture(), copied = path.join(f.external, 'copied');
  fs.mkdirSync(copied);
  fs.writeFileSync(path.join(copied, 'stdout.log'), fs.readFileSync(f.stdout));
  fs.symlinkSync(copied, path.join(f.caseDir, 'redirect'), process.platform === 'win32' ? 'junction' : 'dir');
  f.entry.evidencePins.push({ path: path.join(f.caseDir, 'redirect', 'stdout.log'), sha256: hash(f.stdout) });
  assert.equal(f.inspect().reusable, false);
});

test('same ID exact definitions dedup; ID conflicts are rejected without mutating adapters', () => {
  const f = fixture(), original = structuredClone(f.adapter);
  const result = deduplicateCases([f.adapter, structuredClone(f.adapter)]);
  assert.equal(result.canonical.length, 1);
  assert.equal(result.canonical[0], f.adapter);
  assert.deepEqual(result.aliases, [{ id: f.adapter.id, canonicalId: f.adapter.id }]);
  assert.equal(result.deduplicated[0].reason, 'IDENTICAL_ID_DEFINITION');
  assert.throws(() => deduplicateCases([f.adapter, { ...f.adapter, args: ['--conflict'] }]), { code: 'ADAPTER_ID_CONFLICT' });
  assert.throws(() => deduplicateCases([f.adapter, { ...f.adapter, title: 'Conflicting definition' }]), { code: 'ADAPTER_ID_CONFLICT' });
  assert.deepEqual(f.adapter, original);
});

test('different IDs dedup only with complete identical execution manifests and expose canonical aliases', () => {
  const f = fixture(), first = { ...f.adapter, cacheIdentity: f.identity };
  const second = { ...f.adapter, id: 'synthetic.alias', title: 'Alias title' };
  second.cacheIdentity = f.fingerprint({ adapter: second });
  const result = deduplicateCases([first, second, structuredClone(second)]);
  assert.equal(result.canonical.length, 1);
  assert.equal(result.canonical[0], first);
  assert.equal(result.aliases.length, 2);
  assert.equal(result.deduplicated[0].reason, 'IDENTICAL_EXECUTION_IDENTITY');
  assert.ok(result.aliases.every(row => row.canonicalId === first.id));
});

test('unproven identities, borrowed fingerprints and names suggesting parent/child never skip cases', () => {
  const f = fixture(), first = { ...f.adapter, id: 'synthetic.parent', cacheIdentity: f.identity };
  const child = { ...f.adapter, id: 'synthetic.parent.child', args: ['--child'], cacheIdentity: f.identity };
  const noProof = { ...f.adapter, id: 'synthetic.unproven' };
  assert.equal(deduplicateCases([first, child, noProof]).canonical.length, 3);
  const incomplete = { ...f.adapter, id: 'synthetic.incomplete', cacheIdentity: f.fingerprint({ options: {} }) };
  assert.equal(deduplicateCases([first, incomplete]).canonical.length, 2);
  fs.appendFileSync(f.tool, 'tool drift');
  assert.equal(deduplicateCases([first, { ...f.adapter, id: 'synthetic.stale', cacheIdentity: f.identity }]).canonical.length, 2);
});

// Observe actual byte reads, while keeping every independently declared role.
test('fingerprint byte reads share unchanged inputs only within one call', t => {
  const f = fixture(), file = path.join(f.repoRoot, 'dep.mjs');
  t.after(() => {
    assert.equal(path.dirname(fs.realpathSync(f.root)), fs.realpathSync(EVIDENCE));
    fs.rmSync(f.root, { recursive: true });
  });
  const payload = Buffer.alloc(5 * 1024 * 1024, 0x61);
  fs.writeFileSync(file, payload);
  const originalTimes = fs.statSync(file);
  const originalHash = hash(file);
  const open = fs.openSync, read = fs.readSync, close = fs.closeSync;
  const tracked = new Map(), passes = new Set();
  let serial = 0, bytes = 0;
  t.mock.method(fs, 'openSync', (name, ...args) => {
    const fd = open(name, ...args);
    if (typeof name === 'string' && path.resolve(name) === file && args[0] === 'r') tracked.set(fd, ++serial);
    return fd;
  });
  t.mock.method(fs, 'readSync', (fd, ...args) => {
    const count = read(fd, ...args);
    if (tracked.has(fd) && count > 0) { passes.add(tracked.get(fd)); bytes += count; }
    return count;
  });
  t.mock.method(fs, 'closeSync', fd => { tracked.delete(fd); return close(fd); });
  const iterations = 6, started = process.hrtime.bigint();
  let first;
  for (let i = 0; i < iterations; ++i) {
    const value = f.fingerprint();
    assert.equal(value.cacheable, true);
    if (first) assert.equal(value.identity, first.identity); else first = value;
    assert.deepEqual(value.manifest.inputs.filter(row => row.realPath === file).map(row => row.role).sort(),
      ['dependency-input', 'resolved-input']);
  }
  const elapsedMs = Number(process.hrtime.bigint() - started) / 1e6;
  t.diagnostic('identityReadProbe=' + JSON.stringify({ iterations, payloadBytes: payload.length,
    actualByteReadPasses: passes.size, actualBytesRead: bytes, elapsedMs }));
  assert.equal(passes.size, iterations);
  assert.equal(bytes, payload.length * iterations);
  // A later role cannot inherit approval of a different declared digest.
  const mismatch = f.fingerprint({ options: { ...f.options, resolvedInputs: [{ path: 'dep.mjs', sha256: '0'.repeat(64) }] } });
  assert.equal(mismatch.cacheable, false);
  assert(codes(mismatch).includes('INPUT_SHA256_MISMATCH'));
  // Another call always reads bytes, even when size and mtime are preserved.
  fs.writeFileSync(file, Buffer.alloc(payload.length, 0x62));
  fs.utimesSync(file, originalTimes.atime, originalTimes.mtime);
  const changed = f.fingerprint();
  assert.notEqual(changed.identity, first.identity);
  assert(changed.manifest.inputs.filter(row => row.realPath === file).every(row => row.sha256 !== originalHash));
});

test('fingerprint rechecks metadata before sharing a repeated role read', t => {
  const f = fixture(), file = path.join(f.repoRoot, 'dep.mjs'), originalHash = hash(file);
  t.after(() => {
    assert.equal(path.dirname(fs.realpathSync(f.root)), fs.realpathSync(EVIDENCE));
    fs.rmSync(f.root, { recursive: true });
  });
  const open = fs.openSync, close = fs.closeSync, tracked = new Set();
  let changed = false;
  t.mock.method(fs, 'openSync', (name, ...args) => {
    const fd = open(name, ...args);
    if (typeof name === 'string' && path.resolve(name) === file && args[0] === 'r') tracked.add(fd);
    return fd;
  });
  t.mock.method(fs, 'closeSync', fd => {
    const target = tracked.delete(fd), result = close(fd);
    if (target && !changed) { changed = true; fs.appendFileSync(file, '\nchanged between independently declared roles'); }
    return result;
  });
  const value = f.fingerprint();
  assert(changed);
  assert.equal(value.cacheable, true);
  assert.equal(value.manifest.inputs.find(row => row.role === 'dependency-input').sha256, originalHash);
  assert.equal(value.manifest.inputs.find(row => row.role === 'resolved-input').sha256, hash(file));
  assert.notEqual(value.manifest.inputs.find(row => row.role === 'resolved-input').sha256, originalHash);
});

test('cached inspection shares byte reads only within one call and rejects changes between roles', t => {
  const payload = Buffer.alloc(1024 * 1024 + 17, 0x61);
  const prepare = () => {
    const f = fixture(), file = path.join(f.repoRoot, 'dep.mjs');
    t.after(() => {
      assert.equal(path.dirname(fs.realpathSync(f.root)), fs.realpathSync(EVIDENCE));
      fs.rmSync(f.root, { recursive: true });
    });
    fs.writeFileSync(file, payload);
    const stamp = new Date(1_000_000);
    fs.utimesSync(file, stamp, stamp);
    const identity = f.fingerprint();
    assert.equal(identity.cacheable, true, JSON.stringify(identity.reasons));
    f.entry.identity = identity.identity;
    f.raw.dependencyInputs[0].sha256 = hash(file);
    f.save();
    return { f, file, identity, times: fs.statSync(file), opens: 0, passes: new Set(), bytes: 0 };
  };
  const steady = prepare(), betweenRoles = prepare();
  const probes = new Map([[steady.file, steady], [betweenRoles.file, betweenRoles]]);
  const open = fs.openSync, read = fs.readSync, close = fs.closeSync, tracked = new Map();
  let changedBetweenRoles = false;
  t.mock.method(fs, 'openSync', (name, ...args) => {
    const fd = open(name, ...args);
    const probe = typeof name === 'string' && args[0] === 'r' ? probes.get(path.resolve(name)) : undefined;
    if (probe) tracked.set(fd, { probe, pass: ++probe.opens });
    return fd;
  });
  t.mock.method(fs, 'readSync', (fd, ...args) => {
    const count = read(fd, ...args), target = tracked.get(fd);
    if (target && count > 0) { target.probe.passes.add(target.pass); target.probe.bytes += count; }
    return count;
  });
  t.mock.method(fs, 'closeSync', fd => {
    const target = tracked.get(fd);
    tracked.delete(fd);
    const result = close(fd);
    if (target?.probe === betweenRoles && !changedBetweenRoles) {
      changedBetweenRoles = true;
      fs.appendFileSync(betweenRoles.file, '\nchanged during cached identity inspection');
    }
    return result;
  });
  const inspect = probe => inspectCachedResult({ identity: probe.identity, cacheEntry: probe.f.entry });
  const started = process.hrtime.bigint(), hit = inspect(steady);
  const elapsedMs = Number(process.hrtime.bigint() - started) / 1e6;
  assert.equal(hit.status, 'CACHED_REFERENCE');
  t.diagnostic(JSON.stringify({ payloadBytes: payload.length, actualBytesRead: steady.bytes,
    actualByteReadPasses: steady.passes.size, elapsedMs }));
  assert.equal(steady.bytes, payload.length);
  assert.equal(steady.passes.size, 1);
  assert.equal(steady.opens, 2, 'both declared dependency roles still open the file');

  // Inspection must read again after a same-size replacement with the original mtime.
  fs.writeFileSync(steady.file, Buffer.alloc(payload.length, 0x62));
  fs.utimesSync(steady.file, steady.times.atime, steady.times.mtime);
  const replaced = fs.statSync(steady.file);
  assert.deepEqual([replaced.size, replaced.mtimeMs], [steady.times.size, steady.times.mtimeMs]);
  const bytesBefore = steady.bytes, passesBefore = steady.passes.size;
  const miss = inspect(steady);
  assert.equal(miss.status, 'CACHE_MISS');
  assert.equal(miss.reusable, false);
  assert(codes(miss).includes('INPUT_SHA256_MISMATCH'));
  assert.equal(steady.bytes - bytesBefore, payload.length);
  assert.equal(steady.passes.size - passesBefore, 1);

  // A separate private fixture changes after the first independently declared role closes.
  const raced = inspect(betweenRoles);
  assert.equal(changedBetweenRoles, true);
  assert.equal(betweenRoles.opens, 2);
  assert.equal(betweenRoles.passes.size, 2);
  assert.equal(raced.status, 'CACHE_MISS');
  assert.equal(raced.reusable, false);
  assert(codes(raced).includes('INPUT_SHA256_MISMATCH'));
});


test('cache misses avoid input reads while matching references retain fresh validation', t => {
  const f = fixture(), file = path.join(f.repoRoot, 'dep.mjs');
  t.after(() => {
    assert.equal(path.dirname(fs.realpathSync(f.root)), fs.realpathSync(EVIDENCE));
    fs.rmSync(f.root, { recursive: true });
  });
  const payload = Buffer.alloc(1024 * 1024 + 17, 0x61);
  fs.writeFileSync(file, payload);
  const identity = f.fingerprint();
  f.entry.identity = identity.identity;
  f.raw.dependencyInputs[0].sha256 = hash(file);
  f.save();
  const open = fs.openSync, read = fs.readSync, close = fs.closeSync, opened = new Map();
  let totalBytes = 0, sourceBytes = 0, runtimeBytes = 0;
  t.mock.method(fs, 'openSync', (name, ...args) => {
    const fd = open(name, ...args);
    if (typeof name === 'string' && args[0] === 'r') opened.set(fd, path.resolve(name));
    return fd;
  });
  t.mock.method(fs, 'readSync', (fd, ...args) => {
    const count = read(fd, ...args);
    if (count > 0) {
      totalBytes += count;
      if (opened.get(fd) === file) sourceBytes += count;
      if (opened.get(fd) === process.execPath) runtimeBytes += count;
    }
    return count;
  });
  t.mock.method(fs, 'closeSync', fd => { opened.delete(fd); return close(fd); });
  const begin = process.hrtime.bigint();
  const missing = inspectCachedResult({ identity });
  const elapsedMs = Number(process.hrtime.bigint() - begin) / 1e6;
  assert.deepEqual([missing.reusable, missing.status, missing.actualPassedThisRun], [false, 'CACHE_MISS', false]);
  assert.deepEqual(codes(missing), ['CACHE_ENTRY_MISSING']);
  t.diagnostic(JSON.stringify({ missingEntry: true, payloadBytes: payload.length, totalBytes, sourceBytes, runtimeBytes, elapsedMs }));
  assert.equal(totalBytes, 0, 'an absent entry cannot be reused and needs no source rehash');

  const mismatch = inspectCachedResult({ identity, cacheEntry: { ...f.entry, identity: '0'.repeat(64) } });
  assert.deepEqual([mismatch.reusable, mismatch.status], [false, 'CACHE_MISS']);
  assert.deepEqual(codes(mismatch), ['IDENTITY_MISMATCH']);
  assert.equal(totalBytes, 0, 'an unmatched key cannot enter reference validation');

  // Eligible references still re-open/re-read current source and runtime bytes.
  const hit = inspectCachedResult({ identity, cacheEntry: f.entry });
  assert.deepEqual([hit.status, hit.executedThisRun, hit.actualPassedThisRun], ['CACHED_REFERENCE', false, false]);
  assert.equal(sourceBytes, payload.length);
  assert(runtimeBytes > 0);
  fs.writeFileSync(file, Buffer.alloc(payload.length, 0x62));
  const before = sourceBytes;
  const stale = inspectCachedResult({ identity, cacheEntry: f.entry });
  assert.equal(stale.reusable, false);
  assert(codes(stale).includes('INPUT_SHA256_MISMATCH'));
  assert.equal(sourceBytes - before, payload.length, 'matching references never share bytes across calls');
});


// Wire contract cache mechanics use the existing synthetic fixture. These records
// exercise identity serialization; they do not certify the production tool closure.
test('wire fixed cache effective build and environment records change the original identity', () => {
  const f = fixture();
  const reviewedRecord = {
    compiler: f.tool, linker: f.tool, sdkVersion: 'synthetic-sdk-v1',
    includePaths: [f.external], libraryPaths: [f.external],
    environment: { PATH: f.external, INCLUDE: f.external, LIB: f.external, DOTNET_ROOT: f.external },
    pathContext: { layout: 'synthetic-short-output-v1' }
  };
  const options = { ...f.options, wireFixedContract: reviewedRecord };
  const identity = f.fingerprint({ options });
  assert.equal(identity.cacheable, true);
  f.entry.identity = identity.identity;
  assert.equal(inspectCachedResult({ identity, cacheEntry: f.entry }).status, 'CACHED_REFERENCE');
  const changes = [
    ['compiler', record => { record.compiler += '.changed'; }],
    ['linker', record => { record.linker += '.changed'; }],
    ['sdk selection', record => { record.sdkVersion = 'synthetic-sdk-v2'; }],
    ['include selection', record => { record.includePaths.push(path.join(f.external, 'other-include')); }],
    ['library selection', record => { record.libraryPaths.push(path.join(f.external, 'other-lib')); }],
    ['child environment', record => { record.environment.LIB = path.join(f.external, 'other-lib'); }],
    ['output path class', record => { record.pathContext.layout = 'synthetic-other-output-v2'; }]
  ];
  for (const [label, mutate] of changes) {
    const changed = structuredClone(reviewedRecord);
    mutate(changed);
    const current = f.fingerprint({ options: { ...f.options, wireFixedContract: changed } });
    assert.notEqual(current.identity, identity.identity, label);
    const reference = inspectCachedResult({ identity: current, cacheEntry: f.entry });
    assert.equal(reference.reusable, false, label);
    assert.equal(reference.actualPassedThisRun, false, label);
  }
});

test('wire fixed cache actual path helper excludes per-run names and rejects unsafe context', async () => {
  const { wirePathContext } = await import('../global/controller-lifecycle-pipe/wire-contract.mjs');
  assert.equal(typeof wirePathContext, 'function');
  const f = fixture();
  const firstName = crypto.randomUUID(), secondName = crypto.randomUUID();
  const first = path.join(f.root, 'runs', firstName), second = path.join(f.root, 'runs', secondName);
  fs.mkdirSync(first, { recursive: true });
  fs.mkdirSync(second, { recursive: true });
  const firstContext = wirePathContext(first, f.root), secondContext = wirePathContext(second, f.root);
  assert.ok(firstContext && typeof firstContext === 'object');
  assert.deepEqual(firstContext, secondContext, 'same eligible layout must not key each fresh output name');
  const serialized = JSON.stringify(firstContext);
  assert.equal(serialized.includes(firstName), false);
  assert.equal(serialized.includes(secondName), false);
  assert.deepEqual(JSON.parse(serialized), firstContext, 'the helper returns stable JSON data');
  const initial = f.fingerprint({ options: { ...f.options, wirePathContext: firstContext } });
  const next = f.fingerprint({ options: { ...f.options, wirePathContext: secondContext } });
  assert.equal(initial.identity, next.identity);
  assert.throws(() => wirePathContext(path.join(path.dirname(f.root), 'outside-wire-root'), f.root));
  assert.throws(() => wirePathContext(path.join(f.root, 'runs', 'x'.repeat(240)), f.root));
});

test('wire fixed cache declared source tool and artifact drift refuse existing references', () => {
  for (const field of ['source', 'tool', 'artifact']) {
    const f = fixture();
    const identity = f.fingerprint({ options: { ...f.options, wireFixedContract: { revision: 'synthetic-v1' } } });
    f.entry.identity = identity.identity;
    assert.equal(inspectCachedResult({ identity, cacheEntry: f.entry }).status, 'CACHED_REFERENCE');
    const file = field === 'source' ? path.join(f.repoRoot, 'source.ts') : f[field];
    fs.appendFileSync(file, '\nwire fixed cache drift');
    const stale = inspectCachedResult({ identity, cacheEntry: f.entry });
    assert.equal(stale.reusable, false, field + ' must be re-read even with the old key');
    assert.equal(stale.actualPassedThisRun, false, field);
    const current = f.fingerprint({ options: { ...f.options, wireFixedContract: { revision: 'synthetic-v1' } } });
    assert.notEqual(current.identity, identity.identity, field);
    assert.equal(inspectCachedResult({ identity: current, cacheEntry: f.entry }).reusable, false, field);
  }
});

test('wire fixed cache changed raw logs and structured evidence cannot be hidden by metadata', () => {
  for (const field of ['resultPath', 'stdout', 'stderr', 'details']) {
    const f = fixture();
    assert.equal(f.inspect().status, 'CACHED_REFERENCE');
    fs.appendFileSync(f[field], '\nchanged original evidence');
    f.entry.status = 'PASS';
    f.entry.resultStatus = 'PASS';
    const result = f.inspect();
    assert.equal(result.reusable, false, field);
    assert.equal(result.executedThisRun, false, field);
    assert.equal(result.actualPassedThisRun, false, field);
  }
});

test('wire fixed cache references count zero actual passes and preserve original failure statuses', () => {
  const f = fixture(), original = fs.readFileSync(f.resultPath);
  const reference = f.inspect();
  assert.deepEqual([reference.status, reference.executedThisRun, reference.actualPassedThisRun],
    ['CACHED_REFERENCE', false, false]);
  assert.equal([reference].filter(row => row.status === 'PASS' || row.actualPassedThisRun).length, 0);
  assert.deepEqual(fs.readFileSync(f.resultPath), original, 'reference inspection does not rewrite raw');
  for (const status of ['FAIL', 'UNKNOWN', 'TIMEOUT', 'INCOMPLETE']) {
    f.raw.status = status;
    f.raw.exitCode = 1;
    f.save();
    const failedBytes = fs.readFileSync(f.resultPath);
    f.entry.status = 'PASS';
    const retained = f.inspect();
    assert.deepEqual([retained.hit, retained.reusable, retained.status, retained.previousStatus],
      [false, true, 'UNCHANGED_FAILURE_REFERENCE', status]);
    assert.deepEqual([retained.executedThisRun, retained.actualPassedThisRun], [false, false]);
    assert.deepEqual(fs.readFileSync(f.resultPath), failedBytes);
  }
});

test('wire fixed cache synthetic contract data never replaces explicit closure proof flags', () => {
  const f = fixture();
  for (const flag of ['closureComplete', 'toolClosureComplete']) {
    const identity = f.fingerprint({ options: { ...f.options, wireFixedContract: { reviewed: true }, [flag]: false } });
    assert.equal(identity.cacheable, false, flag);
    assert.equal(identity.identity, null, flag);
    const result = inspectCachedResult({ identity, cacheEntry: f.entry });
    assert.equal(result.status, 'UNCACHEABLE', flag);
    assert.equal(result.reusable, false, flag);
    assert.equal(result.actualPassedThisRun, false, flag);
  }
});


test('wire fixed cache actual resolver ignores installed higher SDK and inherited bootstrap choices', async () => {
  const {resolveWireBuild,wireChildEnvironment} = await import('../global/controller-lifecycle-pipe/wire-contract.mjs');
  const f=fixture(), msvc=path.join(f.external,'VC/Tools/MSVC/14.44.35207'),sdk=path.join(f.external,'Windows Kits/10');
  const bin=path.join(msvc,'bin/Hostx64/x64');
  for(const dir of [bin,path.join(msvc,'include'),path.join(msvc,'lib/x64'),...['ucrt','shared','um','winrt','cppwinrt'].map(n=>path.join(sdk,'Include/10.0.26100.0',n)),...['ucrt','um'].map(n=>path.join(sdk,'Lib/10.0.26100.0',n,'x64'))])fs.mkdirSync(dir,{recursive:true});
  for(const name of ['cl.exe','link.exe'])fs.writeFileSync(path.join(bin,name),'synthetic fixed tool');
  const nativeLedger=path.join(f.repoRoot,'tools/ymcc_sim/global/controller-lifecycle-pipe/native-tool-inputs.json');
  const sharedLedger=path.join(f.repoRoot,'tools/ymcc_sim/global/controller-priority-ledger/tool-inputs.json');
  fs.mkdirSync(path.dirname(nativeLedger),{recursive:true});fs.mkdirSync(path.dirname(sharedLedger),{recursive:true});
  fs.writeFileSync(nativeLedger,JSON.stringify({files:[path.join(msvc,'include/synthetic.h'),path.join(sdk,'Include/10.0.26100.0/um/Windows.h')]}));
  fs.writeFileSync(sharedLedger,JSON.stringify({files:['cl.exe','link.exe'].map(n=>path.join(bin,n))}));
  const first=resolveWireBuild(f.repoRoot);
  fs.mkdirSync(path.join(sdk,'Include/99.0.0.0'),{recursive:true});
  fs.writeFileSync(path.join(msvc,'default-version.txt'),'99.0.0');
  const second=resolveWireBuild(f.repoRoot);
  assert.deepEqual(first,second,'installed enumeration and unbound default files cannot select this fixed compiler');
  const saved={};for(const name of ['CL','_CL_','LINK','VCToolsVersion','WindowsSDKVersion','NODE_OPTIONS','DOTNET_ADDITIONAL_DEPS']){saved[name]=process.env[name];process.env[name]='synthetic-injected';}
  let child;
  try{child=wireChildEnvironment(first,f.caseDir);}finally{for(const [name,value] of Object.entries(saved)){if(value===undefined)delete process.env[name];else process.env[name]=value;}}
  for(const name of Object.keys(saved))assert.equal(Object.hasOwn(child,name),false,name+' must not leak into wire child');
  assert.equal(child.INCLUDE,first.includePaths.join(';'));
  assert.equal(child.LIB,first.libraryPaths.join(';'));
  assert.equal(child.COMSPEC,first.cmd);
  fs.unlinkSync(path.join(bin,'cl.exe'));
  assert.throws(()=>resolveWireBuild(f.repoRoot),/PINNED_PATH_MISSING/,'missing fixed compiler cannot silently select an installed replacement');
});


test('wire fixed cache nested original output pins cannot be omitted changed or redirected',()=>{
 const f=fixture(),nested=path.join(f.caseDir,'original-negative-stdout.json');
 fs.writeFileSync(nested,'{"status":"FAIL","expected":true}');
 fs.writeFileSync(f.details,JSON.stringify({sourceHashes:[pin(nested),{path:'source.ts',sha256:hash(path.join(f.repoRoot,'source.ts'))}]}));
 f.raw.structuredEvidence=[{...pin(f.details),failed:false}];f.outputFiles.push(nested);f.save();
 const adapter={...f.adapter,cacheOutputManifest:'details.json'},identity=f.fingerprint({adapter});f.entry.identity=identity.identity;
 assert.equal(inspectCachedResult({identity,cacheEntry:f.entry}).status,'CACHED_REFERENCE');
 f.entry.evidencePins=f.entry.evidencePins.filter(row=>row.path!==nested);
 assert.equal(inspectCachedResult({identity,cacheEntry:f.entry}).reusable,false,'nested native negative raw is mandatory');
 f.entry.evidencePins.push(pin(nested));fs.appendFileSync(nested,'changed');
 assert.equal(inspectCachedResult({identity,cacheEntry:f.entry}).reusable,false,'changed original raw cannot be hidden by parent PASS');
 fs.writeFileSync(nested,'{"status":"FAIL","expected":true}');
 fs.writeFileSync(f.details,JSON.stringify({sourceHashes:[{path:path.join(f.external,'unreviewed.json'),sha256:'a'.repeat(64)}]}));
 f.raw.structuredEvidence=[{...pin(f.details),failed:false}];f.save();
 assert.equal(inspectCachedResult({identity,cacheEntry:f.entry}).reusable,false,'external graph input must be declared');
});
