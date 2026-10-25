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