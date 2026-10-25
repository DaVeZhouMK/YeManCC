import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { resolveBaseline, resolveCecil, parseArgs, baselinePins, baselineRelativeRoot,
  cecilToolProject, BlockedInputError } from './inputs.mjs';
import { execute } from './run.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '../../../..');
const scratch = process.env.YMCC_FAN_OPEN_TEST_ROOT ?? os.tmpdir();
fs.mkdirSync(scratch, { recursive: true });
const root = fs.mkdtempSync(path.join(scratch, 'fan-open-entry-'));
const fixtureRoot = path.join(repoRoot, baselineRelativeRoot);
function relocatedRepo(name) {
  const repo = path.join(root, name);
  fs.mkdirSync(path.dirname(path.join(repo, baselineRelativeRoot)), { recursive: true });
  fs.cpSync(fixtureRoot, path.join(repo, baselineRelativeRoot), { recursive: true });
  return repo;
}
function approvedProject(repo) {
  const project = path.join(repo, cecilToolProject);
  fs.mkdirSync(path.dirname(project), { recursive: true });
  fs.copyFileSync(path.join(repoRoot, cecilToolProject), project);
  return project;
}
const blocked = code => error => error instanceof BlockedInputError && error.code === code;

test('all CLI parameters parse with exact values', () => {
  assert.deepEqual(parseArgs(['--evidence-dir', 'fresh path', '--hc-assembly', 'hc.dll',
    '--cecil-path', 'cecil.dll', '--legacy-host-root', 'legacy']), {
    evidenceDir: 'fresh path', hcAssembly: 'hc.dll', cecilPath: 'cecil.dll', legacyHostRoot: 'legacy',
  });
});
for (const flag of ['--evidence-dir', '--hc-assembly', '--cecil-path', '--legacy-host-root']) {
  test(`CLI rejects missing and repeated ${flag}`, () => {
    const leading = flag === '--evidence-dir' ? [] : ['--evidence-dir', 'fresh'];
    assert.throws(() => parseArgs([...leading, flag]), /Missing value/);
    assert.throws(() => parseArgs([...leading, flag, '--unknown']), /Missing value/);
    assert.throws(() => parseArgs([...leading, flag, '']), /Missing value/);
    assert.throws(() => parseArgs([...leading, flag, '  ']), /Missing value/);
    assert.throws(() => parseArgs([...leading, flag, 'one', flag, 'two']), /Duplicate argument/);
  });
}
test('CLI rejects unknown flags, positional arguments, and absent required evidence', () => {
  for (const args of [['--unknown', 'x'], ['--evidence-dir', 'fresh', '--payload-root', 'candidate'],
    ['--evidence-dir=fresh'], ['fresh'], ['--evidence-dir', 'fresh', 'unexpected']]) {
    assert.throws(() => parseArgs(args), /Unknown argument/);
  }
  assert.throws(() => parseArgs([]), /--evidence-dir is required/);
});
test('actual CLI rejects before creating evidence', () => {
  const evidence = path.join(root, 'must-not-exist');
  for (const tail of [['--unknown', 'x'], ['--cecil-path'], ['--legacy-host-root', 'a', '--legacy-host-root', 'b']]) {
    const result = spawnSync(process.execPath, [path.join(here, 'run.mjs'), '--evidence-dir', evidence, ...tail],
      { windowsHide: true, encoding: 'utf8' });
    assert.equal(result.status, 2);
    assert.equal(JSON.parse(result.stderr).code, 'FAN_OPEN_ARGUMENT_ERROR');
    assert.equal(fs.existsSync(evidence), false);
  }
});
test('static adapter import and export perform no synchronous filesystem access', () => {
  const script = `import fs from 'node:fs';
    for (const key of ['readFileSync','existsSync','statSync','readdirSync','openSync','accessSync']) {
      fs[key]=()=>{throw Error('unexpected filesystem access: '+key)};
    }
    const {getFanOpenAdapters}=await import(${JSON.stringify(new URL('./adapter.mjs', import.meta.url).href)});
    const first=getFanOpenAdapters();first[0].id='changed';
    const second=getFanOpenAdapters();
    if(second.length!==1||second[0].id!=='fan.open-side-effect-isolation'||JSON.stringify(second[0].blockedExitCodes)!=='[3]')throw Error('static descriptor changed');
    console.log('PASS');`;
  const result = spawnSync(process.execPath, ['--input-type=module', '-e', script],
    { windowsHide: true, encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr);
  assert.equal(result.stdout.trim(), 'PASS');
});
test('fixed baseline resolves after copying only the fixture to a different repo path', () => {
  const moved = relocatedRepo('relocated with spaces');
  const result = resolveBaseline({ repoRoot: moved });
  assert.equal(result.root, path.join(moved, baselineRelativeRoot));
  assert.equal(result.source, 'repository-fixed-fixture');
  assert.deepEqual(result.files.map(file => file.sha256), Object.values(baselinePins));
});
test('explicit legacy root overrides location but never the two pinned identities', () => {
  const moved = relocatedRepo('explicit-valid');
  const override = path.join(moved, baselineRelativeRoot);
  assert.equal(resolveBaseline({ repoRoot, legacyHostRoot: override }).source, 'explicit-legacy-host-root');
  fs.writeFileSync(path.join(override, 'YeManFanBackend.dll'), 'new or corrupted backend');
  assert.throws(() => resolveBaseline({ repoRoot, legacyHostRoot: override }),
    error => error.code === 'FAN_OPEN_BASELINE_IDENTITY' && /Backend/.test(error.message));
});
test('new Host bytes cannot replace the fixed old negative control', () => {
  const moved = relocatedRepo('wrong-host');
  fs.writeFileSync(path.join(moved, baselineRelativeRoot, 'YeManFanHost.dll'), 'candidate-not-5e941');
  assert.throws(() => resolveBaseline({ repoRoot: moved }), error => error.code === 'FAN_OPEN_BASELINE_IDENTITY');
});
test('missing baseline DLL and missing provenance are explicitly BLOCKED', () => {
  const moved = relocatedRepo('missing-baseline');
  fs.unlinkSync(path.join(moved, baselineRelativeRoot, 'YeManFanBackend.dll'));
  assert.throws(() => resolveBaseline({ repoRoot: moved }), blocked('FAN_OPEN_BASELINE_MISSING'));
  fs.unlinkSync(path.join(moved, baselineRelativeRoot, 'provenance.json'));
  assert.throws(() => resolveBaseline({ repoRoot: moved }), blocked('FAN_OPEN_BASELINE_MISSING'));
});
test('changing provenance cannot authorize different baseline hashes', () => {
  const moved = relocatedRepo('forged-provenance');
  const manifestFile = path.join(moved, baselineRelativeRoot, 'provenance.json');
  const manifest = JSON.parse(fs.readFileSync(manifestFile, 'utf8').replace(/^\uFEFF/, ''));
  manifest.files[0].sha256 = '0'.repeat(64);
  fs.writeFileSync(manifestFile, JSON.stringify(manifest));
  assert.throws(() => resolveBaseline({ repoRoot: moved }), error => error.code === 'FAN_OPEN_BASELINE_IDENTITY');
});
test('Cecil discovery follows approved tool hint in relocated local package cache', () => {
  const moved = path.join(root, 'cecil-repo');
  approvedProject(moved);
  const cache = path.join(root, 'nuget cache');
  const local = path.join(cache, 'microsoft.net.illink.tasks', '10.0.11', 'tools', 'net', 'Mono.Cecil.dll');
  fs.mkdirSync(path.dirname(local), { recursive: true });
  fs.copyFileSync(resolveCecil({ repoRoot }).path, local);
  const result = resolveCecil({ repoRoot: moved, env: { NUGET_PACKAGES: cache }, userHome: '' });
  assert.equal(result.path, local);
  assert.equal(result.source, 'approved-repository-tool-dependency');
  const profile = path.join(root, 'new user');
  const profileDll = path.join(profile, '.nuget', 'packages', path.relative(cache, local));
  fs.mkdirSync(path.dirname(profileDll), { recursive: true });
  fs.copyFileSync(local, profileDll);
  assert.equal(resolveCecil({ repoRoot: moved, env: { USERPROFILE: profile }, userHome: '' }).path, profileDll);
});
test('Cecil explicit override is honored; missing explicit DLL never falls back', () => {
  const actual = resolveCecil({ repoRoot });
  assert.equal(resolveCecil({ repoRoot, cecilPath: actual.path }).source, 'explicit-cecil-path');
  assert.throws(() => resolveCecil({ repoRoot, cecilPath: path.join(root, 'missing-Cecil.dll') }),
    blocked('FAN_OPEN_CECIL_MISSING'));
});
test('missing approved project or approved local package is explicitly BLOCKED', () => {
  const moved = path.join(root, 'missing-cecil');
  assert.throws(() => resolveCecil({ repoRoot: moved, env: {}, userHome: '' }), blocked('FAN_OPEN_CECIL_MISSING'));
  approvedProject(moved);
  assert.throws(() => resolveCecil({ repoRoot: moved, env: {}, userHome: '' }), blocked('FAN_OPEN_CECIL_MISSING'));
});
for (const [kind, options, code] of [
  ['baseline', { legacyHostRoot: path.join(root, 'missing-baseline-dir') }, 'FAN_OPEN_BASELINE_MISSING'],
  ['Cecil', { cecilPath: path.join(root, 'missing-Cecil.dll') }, 'FAN_OPEN_CECIL_MISSING'],
]) {
  test(`actual runner writes BLOCKED ${kind} receipt before any build or target execution`, () => {
    const evidenceDir = path.join(root, `blocked-${kind}`);
    const result = execute({ evidenceDir, ...options });
    assert.equal(result.status, 'BLOCKED');
    assert.equal(result.code, code);
    assert.equal(result.hostMainStarted, false);
    assert.equal(result.hardwareCalls, 0);
    assert.deepEqual(fs.readdirSync(evidenceDir), ['result.json']);
  });
}
test('actual CLI preserves BLOCKED status and nonzero exit for missing prerequisite', () => {
  const evidenceDir = path.join(root, 'blocked-cli');
  const result = spawnSync(process.execPath, [path.join(here, 'run.mjs'), '--evidence-dir', evidenceDir,
    '--cecil-path', path.join(root, 'missing-Cecil.dll')], { windowsHide: true, encoding: 'utf8' });
  assert.equal(result.status, 3);
  assert.equal(JSON.parse(result.stdout).status, 'BLOCKED');
  const receipt = JSON.parse(fs.readFileSync(path.join(evidenceDir, 'result.json'), 'utf8'));
  assert.equal(receipt.code, 'FAN_OPEN_CECIL_MISSING');
});


test('existing global runner classifies missing baseline and Cecil as BLOCKED exit 3', async () => {
  const { executeAdapter, validateAdapters } = await import('../runner.mjs');
  const { getFanOpenAdapters } = await import('./adapter.mjs');
  for (const [name, flag, code] of [
    ['baseline', '--legacy-host-root', 'FAN_OPEN_BASELINE_MISSING'],
    ['cecil', '--cecil-path', 'FAN_OPEN_CECIL_MISSING'],
  ]) {
    const adapter = getFanOpenAdapters()[0];
    adapter.args.push(flag, path.join(root, `global-missing-${name}`));
    validateAdapters([adapter], repoRoot);
    const result = await executeAdapter(adapter, { repoRoot, runDir: path.join(root, `global-blocked-${name}`) });
    assert.equal(result.status, 'BLOCKED');
    assert.equal(result.exitCode, 3);
    assert.equal(result.sourceChanged, false);
    assert.equal(result.requiredEvidenceMissing, false);
    const receipt = JSON.parse(fs.readFileSync(path.join(result.evidenceDir, 'fan-open', 'result.json'), 'utf8'));
    assert.equal(receipt.code, code);
    assert.equal(receipt.hostMainStarted, false);
    assert.equal(receipt.hardwareCalls, 0);
  }
});
