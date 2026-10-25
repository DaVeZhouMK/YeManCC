import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {
  createCapturedSourceSnapshot,
  compareSourceSnapshot,
  verifyCapturedSourceSnapshot,
  SNAPSHOT_MANIFEST,
  SNAPSHOT_METADATA,
  SNAPSHOT_POLICY,
} from './source-snapshot.mjs';

// All source, dependency and operational bytes below are synthetic. These tests
// never read an installed app, real configuration or an external source tree.
// Tests within this file are sequential because a few temporarily wrap fs IO.
const digest = bytes => createHash('sha256').update(bytes).digest('hex');
const parse = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const member = (root, relative) => path.join(root, ...relative.split('/'));
const filenameKey = file => {
  if (file instanceof URL) file = fileURLToPath(file);
  if (Buffer.isBuffer(file)) file = file.toString();
  if (typeof file !== 'string') return null;
  const absolute = path.resolve(file);
  return process.platform === 'win32' ? absolute.toLowerCase() : absolute;
};
const hasCode = (...codes) => error => codes.includes(error.code);

function fixture(t) {
  const temporaryRoot = fs.realpathSync(os.tmpdir());
  const base = fs.mkdtempSync(path.join(temporaryRoot, 'ymcc-captured-version-test-'));
  const parent = path.join(base, 'synthetic-workspace', 'Mainline', 'YeManCC-source');
  const repo = path.join(parent, 'SyntheticYMCC');
  const out = path.join(parent, 'CapturedVersion');
  const expectedFiles = new Map();
  const expectedDirectories = new Set();
  fs.mkdirSync(repo, {recursive: true});
  t.after(() => {
    // Validate the absolute owned temporary directory before recursive removal.
    const resolved = path.resolve(base);
    assert.equal(path.dirname(resolved), temporaryRoot);
    assert(path.basename(resolved).startsWith('ymcc-captured-version-test-'));
    assert(!fs.lstatSync(resolved).isSymbolicLink());
    assert.equal(filenameKey(fs.realpathSync(resolved)), filenameKey(resolved));
    fs.rmSync(resolved, {recursive: true, force: true});
  });
  function mkdir(relative) {
    const segments = relative.split('/');
    for (let i = 1; i <= segments.length; i++) {
      expectedDirectories.add(segments.slice(0, i).join('/'));
    }
    fs.mkdirSync(member(repo, relative), {recursive: true});
  }
  function write(relative, value) {
    const bytes = Buffer.isBuffer(value) ? Buffer.from(value) : Buffer.from(value, 'utf8');
    const parentRelative = path.posix.dirname(relative);
    if (parentRelative !== '.') mkdir(parentRelative);
    fs.writeFileSync(member(repo, relative), bytes);
    expectedFiles.set(relative, bytes);
    return member(repo, relative);
  }
  write('native/main.cpp', '// SYNTHETIC complete main; never compiled or run.\r\nint fakeMain = 7;\r\n// unrelated tail is part of identity\r\n');
  write('src/panel.ts', '// SYNTHETIC UI source\nexport const label = "模拟";\n');
  write('tools/synthetic-fixture.mjs', '// SYNTHETIC fixture; never executed\n');
  write('z-last.txt', 'SYNTHETIC final source member\n');
  mkdir('empty');
  const manifestFile = member(out, SNAPSHOT_MANIFEST);
  return {
    base, parent, repo, out, manifestFile, expectedFiles, expectedDirectories, mkdir, write,
    capture(options = {}) {
      return createCapturedSourceSnapshot({repoRoot: repo, outDir: out, ...options});
    },
    compare() {
      return compareSourceSnapshot({repoRoot: repo, manifestFile});
    },
    verify(expectedManifestSha256 = digest(fs.readFileSync(manifestFile))) {
      return verifyCapturedSourceSnapshot({repoRoot: repo, manifestFile, expectedManifestSha256});
    },
  };
}

function assertManifestSaved(f, result) {
  const {manifestFile, ...record} = result;
  assert.equal(filenameKey(manifestFile), filenameKey(f.manifestFile));
  assert.deepEqual(parse(manifestFile), record);
  assert.equal(record.kind, 'ymcc-fixed-source-snapshot');
  assert.equal(record.schemaVersion, 1);
  assert.equal(record.captureMode, 'same-descriptor-capture');
  assert.equal(record.atomicSourceTreeClaimed, false);
  assert.equal(record.requiresFinalCurrentComparison, true);
  assert.equal(record.snapshotPassApprovesChangedSource, false);
  assert.equal(record.executionPerformed, false);
  assert.equal(record.installationPayloadFrozen, false);
  assert.notEqual(record.sourceApprovalGranted, true);
  assert.deepEqual(record.policy, SNAPSHOT_POLICY);
}

function readReference(f, ref, basename) {
  assert.equal(ref.path, `${SNAPSHOT_METADATA}/${basename}`);
  const bytes = fs.readFileSync(member(f.out, ref.path));
  assert.equal(ref.size, bytes.length);
  assert.equal(ref.sha256, digest(bytes));
  return JSON.parse(bytes.toString('utf8'));
}

function expectedRows(expectedFiles) {
  return [...expectedFiles].map(([relative, bytes]) => ({
    path: relative, size: bytes.length, sha256: digest(bytes),
  })).sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);
}

function assertInventory(inventory, files, directories) {
  assert.deepEqual(inventory.files, expectedRows(files));
  assert.deepEqual(inventory.directories, [...directories].sort());
  assert.equal(inventory.fileCount, files.size);
  assert.equal(inventory.totalBytes, [...files.values()].reduce((sum, bytes) => sum + bytes.length, 0));
  assert.deepEqual(inventory.main, expectedRows(files).find(row => row.path === 'native/main.cpp'));
}

function assertCompleteCopy(f, result, files = f.expectedFiles, directories = f.expectedDirectories) {
  assert.deepEqual(result.files, expectedRows(files));
  assert.deepEqual(result.directories, [...directories].sort());
  assert.equal(result.copyVerifiedFiles, files.size);
  const before = readReference(f, result.sourceBefore, 'source-before.json');
  const copied = readReference(f, result.snapshotAfter, 'snapshot-after.json');
  assertInventory(before, files, directories);
  assertInventory(copied, files, directories);
  const foundFiles = [];
  const foundDirectories = [];
  function walk(directory, prefix = '') {
    for (const name of fs.readdirSync(directory)) {
      if (!prefix && name === SNAPSHOT_METADATA) continue;
      const relative = prefix ? `${prefix}/${name}` : name;
      const fullPath = path.join(directory, name);
      const stat = fs.lstatSync(fullPath);
      assert.equal(stat.isSymbolicLink(), false);
      if (stat.isDirectory()) {
        foundDirectories.push(relative);
        walk(fullPath, relative);
      } else {
        assert(stat.isFile());
        foundFiles.push(relative);
        assert(files.has(relative), `Unexpected captured member: ${relative}`);
        assert.deepEqual(fs.readFileSync(fullPath), files.get(relative));
      }
    }
  }
  walk(f.out);
  assert.deepEqual(foundFiles.sort(), [...files.keys()].sort());
  assert.deepEqual(foundDirectories.sort(), [...directories].sort());
  assert.equal(result.snapshotDiff.equal, true);
  assert.deepEqual(result.main.before, before.main);
  assert.deepEqual(result.main.snapshot, copied.main);
}

function assertHistoricalOnly(f, result) {
  const original = fs.readFileSync(f.manifestFile);
  const verified = f.verify(digest(original));
  assert.equal(verified.status, 'FROZEN_SOURCE_VERIFIED');
  assert.equal(verified.snapshotIntegrityVerified, true);
  assert.equal(verified.creationStatus, 'FAIL');
  assert.equal(verified.creationFailure.code, 'SNAPSHOT_SOURCE_OR_COPY_DRIFT');
  assert.equal(verified.isCurrent, false);
  assert.equal(verified.sourceApprovalGranted, false);
  assert.equal(verified.fileCount, result.files.length);
  assert.equal(verified.copyVerifiedFiles, result.files.length);
  assert.equal(verified.snapshotDiff.equal, true);
  assert.equal(verified.createdSourceDiff.equal, false);
  assert.equal(verified.sourceDiff.equal, false);
  assert.deepEqual(fs.readFileSync(f.manifestFile), original);
  assert.throws(() => f.compare(), hasCode('SNAPSHOT_SUCCESSFUL_MANIFEST_REQUIRED'));
  assert.deepEqual(fs.readFileSync(f.manifestFile), original);
  return verified;
}

function withFsOverrides(overrides, action) {
  const originals = {};
  for (const name of Object.keys(overrides)) {
    originals[name] = fs[name];
    fs[name] = overrides[name](originals[name]);
  }
  try { return action(); }
  finally { for (const [name, original] of Object.entries(originals)) fs[name] = original; }
}

// Blocking reads is an oracle, not an alternate inventory implementation: any
// access is retained as a failed attempt even if the API catches the exception.
function forbidContent(paths, action, {forbidDirectoryReads = []} = {}) {
  const forbidden = paths.map(filenameKey);
  const forbiddenDirectories = forbidDirectoryReads.map(filenameKey);
  const attempts = [];
  const reads = [];
  function guard(file, directoryRead = false) {
    const key = filenameKey(file);
    if (key === null) return;
    const prohibited = (directoryRead ? forbiddenDirectories : forbidden)
      .some(root => key === root || key.startsWith(root + path.sep));
    if (prohibited) {
      attempts.push(key);
      throw new Error(`Synthetic excluded-content access: ${key}`);
    }
    if (!directoryRead) reads.push(key);
  }
  const result = withFsOverrides({
    openSync: original => function (file, ...args) { guard(file); return original.call(fs, file, ...args); },
    readFileSync: original => function (file, ...args) { guard(file); return original.call(fs, file, ...args); },
    readdirSync: original => function (file, ...args) { guard(file, true); return original.call(fs, file, ...args); },
  }, () => action(reads));
  assert.deepEqual(attempts, [], 'Excluded synthetic content must never be opened');
  return result;
}

test('captured API preserves every full source byte, independent hashes and empty directories', t => {
  const f = fixture(t);
  const largeMain = Buffer.concat([
    Buffer.from('// SYNTHETIC full main\r\n'),
    Buffer.alloc(2 * 1024 * 1024 + 97, 0x41),
    Buffer.from('\r\nint unrelatedTail = 987;\r\n'),
  ]);
  f.write('native/main.cpp', largeMain);
  f.write('.gitignore', 'native/main.cpp\nsrc/**\ndeps/**\n');
  f.write('deps/synthetic-vendor/original.bin', Buffer.from([0, 255, 10, 13, 128, 42]));
  f.write('InputHost/HcVerbatim/NOTICE', 'SYNTHETIC provenance, not a real vendor payload');
  f.write('src/empty.ts', Buffer.alloc(0));
  f.mkdir('native/empty-nested');
  f.mkdir('deps/synthetic-vendor/empty');
  const events = [];
  const result = f.capture({onProgress: event => events.push({...event})});
  assert.equal(result.status, 'PASS', JSON.stringify(result.failure));
  assert.equal(result.isCurrent, true);
  assertManifestSaved(f, result);
  assertCompleteCopy(f, result);
  assertInventory(readReference(f, result.sourceAfter, 'source-after.json'), f.expectedFiles, f.expectedDirectories);
  assert.equal(result.sourceDiff.equal, true);
  assert.equal(f.compare().status, 'CURRENT');
  const verified = f.verify();
  assert.equal(verified.status, 'FROZEN_SOURCE_VERIFIED');
  assert.equal(verified.creationStatus, 'PASS');
  assert.equal(verified.sourceApprovalGranted, false);
  assert.equal(verified.isCurrent, true);
  assert.deepEqual(verified.main.captured, {path: 'native/main.cpp', size: largeMain.length, sha256: digest(largeMain)});
  const phases = events.map(event => event.phase);
  for (const phase of ['source-before', 'copy-complete', 'before-source-after']) {
    assert.equal(phases.filter(value => value === phase).length, 1);
  }
  assert(phases.indexOf('source-before') < phases.indexOf('copy-complete'));
  assert(phases.indexOf('copy-complete') < phases.indexOf('before-source-after'));
  for (const relative of f.expectedFiles.keys()) {
    const before = events.filter(event => event.phase === 'before-capture-file' && event.path === relative);
    const after = events.filter(event => event.phase === 'after-capture-file' && event.path === relative);
    assert.equal(before.length, 1);
    assert.equal(after.length, 1);
    assert(events.indexOf(before[0]) < events.indexOf(after[0]));
    for (const event of [...before, ...after]) {
      assert.equal(filenameKey(event.sourceRoot), filenameKey(f.repo));
      assert.equal(filenameKey(event.snapshotRoot), filenameKey(f.out));
    }
  }
});

test('changing main after its capture preserves old bytes and only verifies a historical FAIL', t => {
  const f = fixture(t);
  const capturedFiles = new Map(f.expectedFiles);
  let edits = 0;
  const result = f.capture({onProgress: event => {
    if (event.phase === 'after-capture-file' && event.path === 'native/main.cpp') {
      edits++;
      f.write('native/main.cpp', '// SYNTHETIC different full current main\nint laterTail = 314;\n');
    }
  }});
  assert.equal(edits, 1);
  assert.equal(result.status, 'FAIL');
  assert.equal(result.failure.code, 'SNAPSHOT_SOURCE_OR_COPY_DRIFT');
  assert.equal(result.isCurrent, false);
  assertManifestSaved(f, result);
  assertCompleteCopy(f, result, capturedFiles);
  assertInventory(readReference(f, result.sourceAfter, 'source-after.json'), f.expectedFiles, f.expectedDirectories);
  assert.deepEqual(result.sourceDiff.changed.map(row => row.path), ['native/main.cpp']);
  assert.notEqual(result.main.before.sha256, result.main.sourceAfter.sha256);
  const verified = assertHistoricalOnly(f, result);
  assert.deepEqual(verified.main.current, result.main.sourceAfter);
});

test('a revision supplied immediately before capture is the revision actually hashed and saved', t => {
  const f = fixture(t);
  let changes = 0;
  const result = f.capture({onProgress: event => {
    if (event.phase === 'before-capture-file' && event.path === 'native/main.cpp') {
      changes++;
      f.write('native/main.cpp', '// SYNTHETIC revision changed before descriptor open\nint revision = 2;\n');
    }
  }});
  assert.equal(changes, 1);
  assert.equal(result.status, 'PASS', JSON.stringify(result.failure));
  assertCompleteCopy(f, result);
  assert.equal(f.compare().status, 'CURRENT');
});

test('copy-complete callback corruption is rejected even when all current source bytes match', t => {
  const f = fixture(t);
  const result = f.capture({onProgress: event => {
    if (event.phase === 'copy-complete') {
      fs.writeFileSync(member(f.out, 'native/main.cpp'), 'SYNTHETIC corrupted copied main');
    }
  }});
  assert.equal(result.status, 'FAIL');
  assert.equal(result.failure.code, 'SNAPSHOT_SOURCE_OR_COPY_DRIFT');
  assert.equal(result.snapshotDiff.equal, false);
  assert.equal(result.sourceDiff.equal, true);
  assert.deepEqual(result.snapshotDiff.changed.map(row => row.path), ['native/main.cpp']);
  assertManifestSaved(f, result);
  const original = fs.readFileSync(f.manifestFile);
  assert.throws(() => f.verify(), hasCode('SNAPSHOT_CAPTURE_IDENTITY_DIFFERS'));
  assert.throws(() => f.compare(), hasCode('SNAPSHOT_SUCCESSFUL_MANIFEST_REQUIRED'));
  assert.deepEqual(fs.readFileSync(f.manifestFile), original);
  assert.deepEqual(fs.readFileSync(member(f.repo, 'native/main.cpp')), f.expectedFiles.get('native/main.cpp'));
});

test('an after-capture-file exception retains partial FAIL and cannot be verified or reused', t => {
  const f = fixture(t);
  const captured = [];
  const result = f.capture({onProgress: event => {
    if (event.phase !== 'after-capture-file') return;
    captured.push(event.path);
    if (event.path === 'native/main.cpp') {
      throw Object.assign(new Error('SYNTHETIC callback interrupted capture'), {code: 'SYNTHETIC_CAPTURE_ABORT'});
    }
  }});
  assert.equal(result.status, 'FAIL');
  assert.equal(result.failure.code, 'SYNTHETIC_CAPTURE_ABORT');
  assert.equal(result.isCurrent, false);
  assert.equal(result.copyVerifiedFiles, captured.length);
  assert(result.copyVerifiedFiles > 0 && result.copyVerifiedFiles < f.expectedFiles.size);
  assert.equal(fs.existsSync(member(f.out, 'z-last.txt')), false);
  assert.deepEqual(fs.readFileSync(member(f.out, 'native/main.cpp')), f.expectedFiles.get('native/main.cpp'));
  assertManifestSaved(f, result);
  const original = fs.readFileSync(f.manifestFile);
  assert.throws(() => f.verify(), hasCode('SNAPSHOT_COMPLETE_CAPTURE_MANIFEST_REQUIRED'));
  assert.throws(() => f.compare(), hasCode('SNAPSHOT_SUCCESSFUL_MANIFEST_REQUIRED'));
  assert.throws(() => f.capture(), hasCode('SNAPSHOT_FRESH_DESTINATION_REQUIRED'));
  assert.deepEqual(fs.readFileSync(f.manifestFile), original);
});

test('a later change to any snapshot member invalidates its captured-version verification', t => {
  const f = fixture(t);
  const result = f.capture();
  assert.equal(result.status, 'PASS');
  assert.equal(f.verify().snapshotIntegrityVerified, true);
  const original = fs.readFileSync(f.manifestFile);
  fs.writeFileSync(member(f.out, 'src/panel.ts'), '// SYNTHETIC changed non-main copied source');
  assert.throws(() => f.verify(digest(original)), hasCode('SNAPSHOT_CAPTURED_VERSION_CHANGED'));
  const compared = f.compare();
  assert.equal(compared.status, 'STALE');
  assert.equal(compared.isCurrent, false);
  assert.equal(compared.sourceDiff.equal, true);
  assert.deepEqual(compared.snapshotDiff.changed.map(row => row.path), ['src/panel.ts']);
  assert.deepEqual(fs.readFileSync(f.manifestFile), original);
});

test('only the exact imported static policy JSON is opened; runtime settings, sessions and excluded trees are untouched', t => {
  const f = fixture(t);
  const staticPath = 'PowerControl/GameTitlePolicy.json';
  const staticBytes = Buffer.from('{"schema":1,"launchSuffixes":["SYNTHETIC"],"copyrightSuffix":"SYNTHETIC"}\n');
  f.write(staticPath, staticBytes);
  f.write('src/bridge/gamedetect.ts', "// SYNTHETIC static import\nimport policy from '../../PowerControl/GameTitlePolicy.json';\nexport default policy;\n");
  f.write('PowerControl/MemoryScan/catalog.cs', '// SYNTHETIC retained first-party source\n');
  const operational = [
    'PowerControl/runtime/settings.json', 'PowerControl/runtime/session.json',
    'PowerControl/runtime/lease.json', 'PowerControl/runtime/GameTitlePolicy.json',
    'PowerControl/settings.json', 'PowerControl/session/token.fake',
    'PowerControl/GameTitlePolicy.json.bak', 'PowerControl/Runtime.dll',
  ];
  const excludedTrees = ['.git', 'node_modules', 'Build', '_scratch', 'work'];
  for (const relative of operational) f.write(relative, `SYNTHETIC EXCLUDED CONTENT: ${relative}`);
  for (const relative of excludedTrees) f.write(`${relative}/nested/not-source.json`, 'SYNTHETIC EXCLUDED TREE');
  const forbidden = [...operational, ...excludedTrees].flatMap(relative => [member(f.repo, relative), member(f.out, relative)]);
  forbidContent(forbidden, reads => {
    const result = f.capture();
    assert.equal(result.status, 'PASS', JSON.stringify(result.failure));
    assert.equal(result.policy.version, 5);
    assert.equal(result.snapshotDiff.equal, true);
    const dataRow = result.files.find(row => row.path === staticPath);
    assert.deepEqual(dataRow, {path: staticPath, size: staticBytes.length, sha256: digest(staticBytes)});
    assert.deepEqual(fs.readFileSync(member(f.out, staticPath)), staticBytes);
    assert(result.files.some(row => row.path === 'PowerControl/MemoryScan/catalog.cs'));
    for (const relative of operational) {
      assert(result.excluded.some(row => row.path === relative && row.rule === 'powercontrol-nonsource'));
      assert.equal(fs.existsSync(member(f.out, relative)), false);
    }
    for (const relative of excludedTrees) assert.equal(fs.existsSync(member(f.out, relative)), false);
    assert.equal(f.compare().status, 'CURRENT');
    assert.equal(f.verify().snapshotIntegrityVerified, true);
    assert(reads.includes(filenameKey(member(f.repo, staticPath))));
    assert(reads.includes(filenameKey(member(f.out, staticPath))));
  }, {forbidDirectoryReads: excludedTrees.map(relative => member(f.repo, relative))});
});

test('existing output is not overwritten and its synthetic owner bytes remain intact', t => {
  const f = fixture(t);
  fs.mkdirSync(f.out);
  const marker = member(f.out, 'synthetic-owner.txt');
  fs.writeFileSync(marker, 'SYNTHETIC preexisting owner');
  assert.throws(() => f.capture(), hasCode('SNAPSHOT_FRESH_DESTINATION_REQUIRED'));
  assert.equal(fs.readFileSync(marker, 'utf8'), 'SYNTHETIC preexisting owner');
  assert.equal(fs.existsSync(f.manifestFile), false);
});

test('source and output root junction aliases are rejected before traversal', t => {
  const f = fixture(t);
  const alias = path.join(f.parent, 'SyntheticAlias');
  const target = path.join(f.parent, 'SyntheticOutsideOutput');
  fs.symlinkSync(f.repo, alias, process.platform === 'win32' ? 'junction' : 'dir');
  assert.throws(() => createCapturedSourceSnapshot({repoRoot: alias, outDir: f.out}), hasCode('SNAPSHOT_REPARSE_PATH', 'SNAPSHOT_REPARSE_RESOLUTION'));
  assert.equal(fs.existsSync(f.out), false);
  fs.mkdirSync(target);
  const marker = path.join(target, 'owner.txt');
  fs.writeFileSync(marker, 'SYNTHETIC outside root owner');
  fs.symlinkSync(target, f.out, process.platform === 'win32' ? 'junction' : 'dir');
  forbidContent([marker], () => {
    assert.throws(() => f.capture(), hasCode('SNAPSHOT_REPARSE_PATH', 'SNAPSHOT_REPARSE_RESOLUTION'));
  });
  assert.deepEqual(fs.readdirSync(target), ['owner.txt']);
  assert.equal(fs.readFileSync(marker, 'utf8'), 'SYNTHETIC outside root owner');
});

test('a source file symlink cannot expose even explicitly synthetic external bytes', t => {
  const f = fixture(t);
  const external = path.join(f.base, 'synthetic-external.cpp');
  fs.writeFileSync(external, '// SYNTHETIC external bytes must never be opened\n');
  const link = member(f.repo, 'native/linked.cpp');
  try {
    fs.symlinkSync(external, link, 'file');
  } catch (error) {
    // Windows file symlinks need a privilege that directory junctions do not.
    // Do not substitute a mock lstat or a junction and call that file coverage.
    if (process.platform !== 'win32' || error.code !== 'EPERM') throw error;
    t.diagnostic('Real file-symlink setup returned EPERM; this boundary is unverified on this Windows token. Real directory-junction tests still execute separately.');
    t.skip('Windows token cannot create a real file symlink (EPERM)');
    return;
  }
  assert.equal(fs.lstatSync(link).isSymbolicLink(), true);
  forbidContent([external, link], () => {
    const result = f.capture();
    assert.equal(result.status, 'FAIL');
    assert(['SNAPSHOT_REPARSE_PATH', 'SNAPSHOT_REPARSE_RESOLUTION'].includes(result.failure.code));
    assert.equal(result.isCurrent, false);
    assert.equal(fs.existsSync(member(f.out, 'native/linked.cpp')), false);
    assert.throws(() => f.verify(), hasCode('SNAPSHOT_COMPLETE_CAPTURE_MANIFEST_REQUIRED'));
  });
  assert.equal(fs.readFileSync(external, 'utf8'), '// SYNTHETIC external bytes must never be opened\n');
});

test('an included directory junction is rejected without descending into its external target', t => {
  const f = fixture(t);
  const external = path.join(f.base, 'synthetic-external-directory');
  fs.mkdirSync(external);
  fs.writeFileSync(path.join(external, 'source.cpp'), '// SYNTHETIC external');
  fs.symlinkSync(external, member(f.repo, 'linked-directory'), process.platform === 'win32' ? 'junction' : 'dir');
  forbidContent([external], () => {
    const result = f.capture();
    assert.equal(result.status, 'FAIL');
    assert(['SNAPSHOT_REPARSE_PATH', 'SNAPSHOT_REPARSE_RESOLUTION'].includes(result.failure.code));
    assert.equal(fs.existsSync(member(f.out, 'linked-directory')), false);
  }, {forbidDirectoryReads: [external, member(f.repo, 'linked-directory')]});
});

test('modification during the actual source read is rejected as an incomplete capture', t => {
  const f = fixture(t);
  const originalMain = Buffer.alloc(2 * 1024 * 1024 + 37, 0x53);
  f.write('native/main.cpp', originalMain);
  const sourcePath = member(f.repo, 'native/main.cpp');
  const sourceKey = filenameKey(sourcePath);
  const sourceDescriptors = new Set();
  let capturingMain = false;
  let interruptedReads = 0;
  const result = withFsOverrides({
    openSync: original => function (file, ...args) {
      const descriptor = original.call(fs, file, ...args);
      if (capturingMain && filenameKey(file) === sourceKey) sourceDescriptors.add(descriptor);
      return descriptor;
    },
    closeSync: original => function (descriptor) {
      sourceDescriptors.delete(descriptor);
      return original.call(fs, descriptor);
    },
    readSync: original => function (descriptor, ...args) {
      const count = original.call(fs, descriptor, ...args);
      if (capturingMain && sourceDescriptors.has(descriptor) && count > 0 && interruptedReads === 0) {
        interruptedReads++;
        // A deterministic size change occurs between two reads on the real fd.
        fs.appendFileSync(sourcePath, '\nSYNTHETIC concurrent edit\n');
      }
      return count;
    },
  }, () => f.capture({onProgress: event => {
    if (event.path === 'native/main.cpp' && event.phase === 'before-capture-file') capturingMain = true;
    if (event.path === 'native/main.cpp' && event.phase === 'after-capture-file') capturingMain = false;
  }}));
  assert.equal(interruptedReads, 1, 'The real source descriptor must have been read and changed');
  assert.equal(sourceDescriptors.size, 0, 'Source handles must close on failure');
  assert.equal(result.status, 'FAIL');
  assert.equal(result.failure.code, 'SNAPSHOT_FILE_CHANGED_DURING_CAPTURE');
  assert.equal(result.isCurrent, false);
  assert(fs.statSync(sourcePath).size > originalMain.length);
  assert.equal(fs.existsSync(member(f.out, 'z-last.txt')), false);
  assertManifestSaved(f, result);
  assert.throws(() => f.verify(), hasCode('SNAPSHOT_COMPLETE_CAPTURE_MANIFEST_REQUIRED'));
  assert.throws(() => f.compare(), hasCode('SNAPSHOT_SUCCESSFUL_MANIFEST_REQUIRED'));
});

test('independent target hashing catches a corrupt write despite a correct source-stream hash', t => {
  const f = fixture(t);
  const targetKey = filenameKey(member(f.out, 'native/main.cpp'));
  const outputDescriptors = new Set();
  let corruptedWrites = 0;
  const result = withFsOverrides({
    openSync: original => function (file, flags, ...args) {
      const descriptor = original.call(fs, file, flags, ...args);
      if (filenameKey(file) === targetKey && typeof flags === 'number' && (flags & fs.constants.O_WRONLY)) {
        outputDescriptors.add(descriptor);
      }
      return descriptor;
    },
    closeSync: original => function (descriptor) {
      outputDescriptors.delete(descriptor);
      return original.call(fs, descriptor);
    },
    writeSync: original => function (descriptor, buffer, ...args) {
      if (outputDescriptors.has(descriptor) && Buffer.isBuffer(buffer) && corruptedWrites === 0) {
        const corrupted = Buffer.from(buffer);
        corrupted[args[0] ?? 0] ^= 1;
        corruptedWrites++;
        return original.call(fs, descriptor, corrupted, ...args);
      }
      return original.call(fs, descriptor, buffer, ...args);
    },
  }, () => f.capture());
  assert.equal(corruptedWrites, 1);
  assert.equal(outputDescriptors.size, 0);
  assert.equal(result.status, 'FAIL');
  assert.equal(result.failure.code, 'SNAPSHOT_COPIED_BYTES_DIFFER');
  assert.deepEqual(fs.readFileSync(member(f.repo, 'native/main.cpp')), f.expectedFiles.get('native/main.cpp'));
  assert.notDeepEqual(fs.readFileSync(member(f.out, 'native/main.cpp')), f.expectedFiles.get('native/main.cpp'));
  assert.throws(() => f.verify(), hasCode('SNAPSHOT_COMPLETE_CAPTURE_MANIFEST_REQUIRED'));
});

test('a callback cannot prepopulate a target file and have it silently overwritten', t => {
  const f = fixture(t);
  let writes = 0;
  const result = f.capture({onProgress: event => {
    if (event.phase === 'before-capture-file' && event.path === 'native/main.cpp') {
      writes++;
      fs.mkdirSync(member(f.out, 'native'), {recursive: true});
      fs.writeFileSync(member(f.out, 'native/main.cpp'), 'SYNTHETIC other output owner');
    }
  }});
  assert.equal(writes, 1);
  assert.equal(result.status, 'FAIL');
  assert.equal(result.failure.code, 'SNAPSHOT_COPY_OVERWRITE_REJECTED');
  assert.equal(fs.readFileSync(member(f.out, 'native/main.cpp'), 'utf8'), 'SYNTHETIC other output owner');
  assert.throws(() => f.verify(), hasCode('SNAPSHOT_COMPLETE_CAPTURE_MANIFEST_REQUIRED'));
});

test('post-capture source additions and removals remain historical versions rather than current approval', t => {
  for (const operation of ['add', 'remove']) {
    const f = fixture(t);
    const capturedFiles = new Map(f.expectedFiles);
    const capturedDirectories = new Set(f.expectedDirectories);
    let edits = 0;
    const result = f.capture({onProgress: event => {
      if (event.phase !== 'before-source-after') return;
      edits++;
      if (operation === 'add') f.write('new-source/late.ts', '// SYNTHETIC later source');
      else {
        fs.unlinkSync(member(f.repo, 'src/panel.ts'));
        f.expectedFiles.delete('src/panel.ts');
      }
    }});
    assert.equal(edits, 1);
    assert.equal(result.status, 'FAIL');
    assert.equal(result.failure.code, 'SNAPSHOT_SOURCE_OR_COPY_DRIFT');
    assertCompleteCopy(f, result, capturedFiles, capturedDirectories);
    if (operation === 'add') assert.deepEqual(result.sourceDiff.added, ['new-source/late.ts']);
    else assert.deepEqual(result.sourceDiff.removed, ['src/panel.ts']);
    assertHistoricalOnly(f, result);
  }
});

test('verification pins the original manifest and independently checks saved inventory bytes', t => {
  const f = fixture(t);
  assert.equal(f.capture().status, 'PASS');
  const manifestBytes = fs.readFileSync(f.manifestFile);
  assert.throws(() => f.verify('0'.repeat(64)), hasCode('SNAPSHOT_EXPECTED_MANIFEST_SHA_REQUIRED'));
  assert.deepEqual(fs.readFileSync(f.manifestFile), manifestBytes);
  const manifest = parse(f.manifestFile);
  fs.appendFileSync(member(f.out, manifest.sourceBefore.path), ' ');
  assert.throws(() => f.verify(digest(manifestBytes)), hasCode('SNAPSHOT_EVIDENCE_HASH_CHANGED'));
  assert.deepEqual(fs.readFileSync(f.manifestFile), manifestBytes);
});

test('a tree without the complete main cannot qualify as a captured source version', t => {
  const f = fixture(t);
  fs.unlinkSync(member(f.repo, 'native/main.cpp'));
  const result = f.capture();
  assert.equal(result.status, 'FAIL');
  assert.equal(result.failure.code, 'SNAPSHOT_MAIN_CPP_REQUIRED');
  assert.equal(result.isCurrent, false);
  assertManifestSaved(f, result);
  assert.throws(() => f.verify(), hasCode('SNAPSHOT_COMPLETE_CAPTURE_MANIFEST_REQUIRED'));
});
