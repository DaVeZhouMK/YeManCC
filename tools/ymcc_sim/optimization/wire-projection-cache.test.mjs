import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { WIRE_PROJECTION_CONTRACT, WIRE_PROJECTION_INPUTS, wireProjectionForManifest, wireIdentityManifest } from '../global/controller-lifecycle-pipe/cache-projection.mjs';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const workspace = path.resolve('G:/YeManCC-Work/Testing/YMCC/Workspace');
const casePrefix = 'wire-projection-case-';
const samePath = (a, b) => process.platform === 'win32' ? a.toLowerCase() === b.toLowerCase() : a === b;
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
let sourceBytes;
const mainRelative = 'native/main.cpp';
const templateRelative = 'tools/fixtures/input_lifecycle_integration/NativeEntry.cpp';
const helperRelative = 'tools/ymcc_sim/global/controller-lifecycle-pipe/cache-projection.mjs';

const wireManifest = (repoRoot, inputs) => ({ repoRoot, adapter: {
  kind: 'node', path: 'tools/ymcc_sim/global/controller-lifecycle-pipe/run.mjs',
  args: ['--evidence-dir', '{outDir}/lifecycle-pipe'], cacheOutputManifest: 'lifecycle-pipe/result.json',
  sourceProjectionContract: WIRE_PROJECTION_CONTRACT
}, inputs });

function fixture(t) {
  sourceBytes ??= new Map(WIRE_PROJECTION_INPUTS.map(relative => [relative, fs.readFileSync(path.join(repo, relative))]));
  assert(!fs.lstatSync(workspace).isSymbolicLink() && samePath(fs.realpathSync(workspace), workspace));
  const root = fs.mkdtempSync(path.join(workspace, casePrefix));
  const created = fs.lstatSync(root, { bigint: true });
  t.after(() => {
    assert.equal(path.dirname(path.resolve(root)), workspace);
    assert(path.basename(root).startsWith(casePrefix));
    const stat = fs.lstatSync(root, { bigint: true });
    assert(stat.isDirectory() && !stat.isSymbolicLink() && stat.ino === created.ino && stat.dev === created.dev);
    assert(samePath(fs.realpathSync(root), root));
    fs.rmSync(root, { recursive: true, force: false });
  });
  const repoRoot = path.join(root, 'repo');
  for (const [relative, bytes] of sourceBytes) {
    const file = path.join(repoRoot, relative);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, bytes, { flag: 'wx' });
  }
  const inputs = WIRE_PROJECTION_INPUTS.map(relative => {
    const file = path.resolve(repoRoot, relative);
    return { role: 'source', path: file, realPath: fs.realpathSync(file), sha256: sha(fs.readFileSync(file)) };
  });
  const manifest = wireManifest(repoRoot, inputs);
  const update = (relative, bytes) => {
    const file = path.resolve(repoRoot, relative);
    fs.writeFileSync(file, bytes);
    const row = manifest.inputs.find(row => row.path === file);
    row.sha256 = sha(fs.readFileSync(file));
    row.realPath = fs.realpathSync(file);
  };
  const projected = () => ({ ...structuredClone(manifest), sourceProjection: wireProjectionForManifest(manifest) });
  return { root, repoRoot, manifest, update, projected };
}
const identity = manifest => sha(JSON.stringify(wireIdentityManifest(manifest)));

test('wire projection cache preserves full main pins outside the normalized identity copy', t => {
  assert.equal(WIRE_PROJECTION_CONTRACT, 'wire-native-main-v1');
  assert.equal(WIRE_PROJECTION_INPUTS.length, 8);
  assert.equal(new Set(WIRE_PROJECTION_INPUTS).size, 8);
  const f = fixture(t), manifest = f.projected(), original = structuredClone(manifest);
  const fullMain = manifest.inputs.find(row => row.path === path.join(f.repoRoot, mainRelative));
  assert.equal(fullMain.sha256, sha(sourceBytes.get(mainRelative)));
  const normalized = wireIdentityManifest(manifest);
  assert.notStrictEqual(normalized, manifest);
  assert.deepEqual(manifest, original, 'normalization cannot rewrite the externally retained full manifest');
  assert.notEqual(normalized.inputs.find(row => row.path === fullMain.path).sha256, fullMain.sha256);
  assert.equal(manifest.sourceProjection.generated.variants.length, 3);
  const sourcePins = [...sourceBytes].map(([relative, bytes]) => ({ path: path.join(repo, relative), bytes: bytes.length, sha256: sha(bytes) }));
  t.diagnostic(JSON.stringify({ actualInputPins: sourcePins, copiedInputs: 8, businessPass: null }));
});

test('wire projection cache unrelated CPU marker changes full main hash but preserves identity', t => {
  const f = fixture(t), before = f.projected(), beforeBytes = JSON.stringify(before);
  const changed = Buffer.concat([sourceBytes.get(mainRelative), Buffer.from('\n// unrelated CPU/CCD identity boundary control\n')]);
  f.update(mainRelative, changed);
  const after = f.projected();
  assert.notEqual(after.inputs.find(row => row.path.endsWith(path.join('native', 'main.cpp'))).sha256,
    before.inputs.find(row => row.path.endsWith(path.join('native', 'main.cpp'))).sha256);
  assert.equal(identity(before), identity(after));
  assert.equal(JSON.stringify(before), beforeBytes, 'the previous full-source evidence remains unchanged');
});

test('wire projection cache related request and template changes alter normalized identity', t => {
  const f = fixture(t), before = f.projected(), baseline = identity(before);
  const main = sourceBytes.get(mainRelative).toString('utf8');
  const anchor = 'if (advanceSequence && hostConsumed) g_inputHostSequence = sequenceValue;';
  assert.equal(main.split(anchor).length, 2, 'one actual request anchor');
  f.update(mainRelative, main.replace(anchor, anchor + ' /* identity-relevant request edit */'));
  assert.notEqual(identity(f.projected()), baseline);
  f.update(mainRelative, sourceBytes.get(mainRelative));
  f.update(templateRelative, Buffer.concat([sourceBytes.get(templateRelative), Buffer.from('\n// identity-relevant template edit\n')]));
  assert.notEqual(identity(f.projected()), baseline);
});

test('wire projection cache refuses missing main or helper input declarations', t => {
  const f = fixture(t);
  for (const relative of [mainRelative, helperRelative]) {
    const file = path.resolve(f.repoRoot, relative);
    const manifest = { ...f.manifest, inputs: f.manifest.inputs.filter(row => row.path !== file) };
    assert.throws(() => wireProjectionForManifest(manifest), /WIRE_PROJECTION_/, relative);
  }
});

test('wire projection cache refuses stale actual main and helper hashes', t => {
  const f = fixture(t);
  for (const relative of [mainRelative, helperRelative]) {
    const file = path.resolve(f.repoRoot, relative), original = fs.readFileSync(file);
    fs.appendFileSync(file, '\n// unpinned change\n');
    assert.throws(() => wireProjectionForManifest(f.manifest), /WIRE_PROJECTION_SOURCE_CHANGED/, relative);
    fs.writeFileSync(file, original);
  }
});

test('wire projection cache refuses a private source directory redirected outside its repo root', t => {
  const f = fixture(t), native = path.join(f.repoRoot, 'native'), outside = path.join(f.root, 'outside-repo');
  fs.mkdirSync(outside);
  fs.writeFileSync(path.join(outside, 'main.cpp'), sourceBytes.get(mainRelative), { flag: 'wx' });
  // Both sides are owned by this case. No external target or product is modified.
  fs.unlinkSync(path.join(native, 'main.cpp'));
  fs.rmdirSync(native);
  fs.symlinkSync(outside, native, process.platform === 'win32' ? 'junction' : 'dir');
  const mainRow = f.manifest.inputs.find(row => row.path === path.join(native, 'main.cpp'));
  mainRow.realPath = fs.realpathSync(mainRow.path);
  assert.throws(() => wireProjectionForManifest(f.manifest), /WIRE_PROJECTION_SOURCE_OUTSIDE_ROOT/);
});

test('wire projection cache refuses false descriptors and unknown contracts', () => {
  // Invalid descriptors must be rejected before the projection reads any source.
  const base = wireManifest(workspace, []);
  for (const patch of [
    { kind: 'powershell' }, { path: 'tools/other-wire.mjs' },
    { args: ['--evidence-dir', '{outDir}/different-scope'] },
    { cacheOutputManifest: 'different/result.json' }, { sourceProjectionContract: 'unknown-wire-contract' }
  ]) {
    const manifest = { ...base, adapter: { ...base.adapter, ...patch } };
    assert.throws(() => wireProjectionForManifest(manifest), /WIRE_PROJECTION_DESCRIPTOR_CHANGED/);
  }
});

test('wire projection cache leaves ordinary adapters without a projection contract unchanged', () => {
  const manifest = { repoRoot: workspace, adapter: { kind: 'node', path: 'ordinary.mjs', args: [] }, inputs: [] };
  const before = structuredClone(manifest);
  assert.equal(wireProjectionForManifest(manifest), null);
  assert.strictEqual(wireIdentityManifest(manifest), manifest);
  assert.deepEqual(manifest, before);
});
