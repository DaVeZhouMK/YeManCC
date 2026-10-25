import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import test from 'node:test';
import { getAdapters, ADAPTER_EXCLUSIONS } from './adapters.mjs';
import { getSleepAdapters } from './sleep.mjs';
import { validateAdapters, fingerprintInputs } from './runner.mjs';
import { lockBundledNodeTools, verifyBundledToolScope, classifyBundledInput } from './bundled-node-tool-authority.mjs';

const repoRoot = fs.realpathSync(fileURLToPath(new URL('../../../', import.meta.url)));
const require = createRequire(path.join(repoRoot, 'package.json'));
const inventory = () => getAdapters(repoRoot);
const sourceContracts = new Set([
  'steam.shared-mode-source', 'steam.contact-source', 'performance.snapshot-bounds-source',
]);
const domains = new Set([
  'controller', 'gyro', 'touch', 'fan', 'lifecycle', 'steam', 'performance',
  'sensor', 'battery', 'audio', 'game-library', 'plugins', 'display', 'settings', 'sleep',
]);
// These are existing reviewed producer budgets, not permission to inflate others.
const longProducerBudgets = new Map([
  ['controller.signal-evidence', 1800000],
  ['sleep.gyro-power', 120000],
]);
const explicitOutputArguments = new Map([
  ['lifecycle.ui-power-transport', ['--out-dir', '{outDir}']],
  ['lifecycle.ui-power-recovery', ['--out-dir', '{outDir}']],
  ['audio.music-player', ['--evidence-dir', '{outDir}/music-player']],
]);
function assertRepoFile(relative) {
  assert.equal(typeof relative, 'string');
  assert(!path.isAbsolute(relative), relative);
  assert(!relative.includes('\\') && !relative.split('/').includes('..'), relative);
  const actual = fs.realpathSync(path.resolve(repoRoot, relative));
  const rel = path.relative(repoRoot, actual);
  assert(rel && !path.isAbsolute(rel) && rel !== '..' && !rel.startsWith(`..${path.sep}`), relative);
  assert(fs.statSync(actual).isFile(), relative);
}
function assertOutputContract(adapter) {
  const expected = explicitOutputArguments.get(adapter.id) ?? (adapter.resultFiles?.length ? ['{outDir}'] : []);
  assert.deepEqual(adapter.args, expected, adapter.id);
  assert(!adapter.args.some(arg => /real-backend|allow-hardware|SetSuspendState|ExecutionPolicy|\bBypass\b|^-Attach$|^-StartApp$|^-WithApp$/i.test(arg)), adapter.id);
  for (const result of adapter.resultFiles ?? []) {
    assert.match(result, /^[a-z0-9-]+(?:\/[a-z0-9-]+)*\.json$/, adapter.id);
    assert(!path.isAbsolute(result) && !result.split('/').includes('..') && !result.includes('\\'), adapter.id);
  }
  assert(!adapter.needsOriginalLocation, adapter.id);
}
function registrationIdentity({ artifactInputs, blockedReason, ...identity }) {
  // Tools are resolved against the requested checkout. Their absence cannot
  // erase IDs, reorder tests, change argv or manufacture an execution result.
  return identity;
}

test('every allowlisted adapter has a unique id and a concrete repository test/source', () => {
  const adapters = inventory(), ids = new Set(), paths = new Set();
  assert(adapters.length > 0);
  assert.equal(validateAdapters(adapters, repoRoot), adapters);
  for (const adapter of adapters) {
    assert.match(adapter.id, /^[a-z0-9][a-z0-9.-]{0,99}$/);
    assert(!ids.has(adapter.id), adapter.id); ids.add(adapter.id);
    assert(!paths.has(adapter.path), adapter.path); paths.add(adapter.path);
    assert(domains.has(adapter.domain), `${adapter.id}: ${adapter.domain}`);
    assert(adapter.title.trim());
    assert(['node', 'typescript'].includes(adapter.kind));
    assert.equal(path.extname(adapter.path), adapter.kind === 'node' ? '.mjs' : '.ts');
    assertRepoFile(adapter.path);
    assert(adapter.evidenceInputs.length > 0, adapter.id);
    assert.equal(new Set(adapter.evidenceInputs).size, adapter.evidenceInputs.length, adapter.id);
    for (const input of adapter.evidenceInputs) assertRepoFile(input);
    assert(adapter.evidenceInputs.some(input => /^(src|native|CustomSteamLibrary|public)\//.test(input)), adapter.id);
    assert.deepEqual(adapter.profiles, adapter.id === 'sleep.gyro-power' ? ['desktop', 'full', 'sleep'] : ['desktop', 'full']);
    assert(Number.isInteger(adapter.timeoutMs) && adapter.timeoutMs >= 100 && adapter.timeoutMs <= 3600000, adapter.id);
    if (longProducerBudgets.has(adapter.id)) assert.equal(adapter.timeoutMs, longProducerBudgets.get(adapter.id), adapter.id);
    else assert(adapter.timeoutMs <= 60000, `${adapter.id}: unreviewed longer producer budget`);
    assert.equal(adapter.scope, sourceContracts.has(adapter.id) ? 'source-contract' : 'behavioral-mock', adapter.id);
  }
  assert.deepEqual(new Set(adapters.map(adapter => adapter.domain)), domains);
});

test('runner enforces both existing timeout bounds and rejects invalid budgets without execution', () => {
  const original = inventory()[0];
  for (const timeoutMs of [100, 101, 1000, 60000, 120000, 1800000, 3600000]) {
    const adapter = { ...original, timeoutMs };
    assert.equal(validateAdapters([adapter], repoRoot)[0].timeoutMs, timeoutMs);
  }
  for (const timeoutMs of [99, 0, -1, 3600001, Number.MAX_SAFE_INTEGER, 100.5, NaN, Infinity, -Infinity, '60000', null, undefined]) {
    assert.throws(() => validateAdapters([{ ...original, timeoutMs }], repoRoot), /ADAPTER_TIMEOUT_INVALID/, String(timeoutMs));
  }
});

test('sleep adapters have one owner and source contracts do not claim behavioral coverage', () => {
  const base = inventory(), sleep = getSleepAdapters(repoRoot), combined = [...base, ...sleep];
  assert.deepEqual(base.filter(adapter => adapter.domain === 'sleep').map(adapter => adapter.id), ['sleep.gyro-power']);
  assert(!base.some(adapter => adapter.id === 'sleep.task-policy-source'));
  assert(sleep.some(adapter => adapter.id === 'sleep.task-policy-source' && adapter.scope === 'source-contract'));
  assert(!sleep.some(adapter => adapter.id === 'sleep.gyro-power'));
  assert.equal(new Set(combined.map(adapter => adapter.id)).size, combined.length);
  assert.equal(validateAdapters(combined, repoRoot), combined);
  assert.deepEqual(new Set(base.filter(adapter => adapter.scope === 'source-contract').map(adapter => adapter.id)), sourceContracts);
  for (const domain of domains) assert(base.some(adapter => adapter.domain === domain && adapter.scope === 'behavioral-mock'), domain);
});

test('missing checkout files retain IDs, order and argv while unavailable tools and sources stay blocked', () => {
  const missingRoot = path.join(repoRoot, '__missing_checkout__');
  assert(!fs.existsSync(missingRoot));
  const present = inventory(), missing = getAdapters(missingRoot);
  assert.deepEqual(missing.map(registrationIdentity), present.map(registrationIdentity));
  assert.deepEqual(missing.map(adapter => adapter.id), present.map(adapter => adapter.id));
  assert.equal(validateAdapters(missing, missingRoot), missing);
  for (const adapter of missing) {
    assert(!Object.hasOwn(adapter, 'status'), adapter.id);
    const inputs = fingerprintInputs(missingRoot, [adapter.path, ...adapter.evidenceInputs]);
    assert(inputs.length > 0 && inputs.every(input => input.status === 'MISSING_OR_OUTSIDE_REPOSITORY' && input.sha256 === null), adapter.id);
    if (Object.hasOwn(adapter, 'artifactInputs')) {
      assert.deepEqual(adapter.artifactInputs, [], adapter.id);
      assert.equal(typeof adapter.blockedReason, 'string', adapter.id);
      assert(adapter.blockedReason.length > 0, adapter.id);
    }
  }
  assert(missing.some(adapter => adapter.blockedReason));
});

test('inventories and nested argument/source/profile arrays are independent between calls', () => {
  const one = inventory(), before = inventory();
  one[0].profiles.push('unexpected'); one[0].evidenceInputs.length = 0; one[0].args.push('--unexpected');
  one.find(adapter => adapter.resultFiles)?.resultFiles.push('unexpected.json');
  one[0].title = 'changed'; one.pop();
  assert.deepEqual(inventory(), before);
});

test('only reviewed owned output arguments and relative result files are supplied; no native or machine flags', () => {
  for (const adapter of inventory()) {
    assertOutputContract(adapter);
    if (adapter.kind === 'typescript') {
      const source = fs.readFileSync(path.join(repoRoot, adapter.path), 'utf8');
      assert.doesNotMatch(source, /import\.meta\.(?:url|dirname|filename)|\b__(?:dirname|filename)\b/, adapter.id);
    }
  }
});

test('unknown flags, output escapes and machine arguments cannot pass the output contract', () => {
  const adapter = inventory().find(row => row.id === 'lifecycle.ui-power-transport');
  for (const args of [[], ['{outDir}'], ['--out-dir', '../foreign'], ['--out-dir', '{outDir}', '--allow-hardware'], ['--out-dir', '{outDir}', '-Attach']]) {
    assert.throws(() => assertOutputContract({ ...adapter, args }));
  }
  for (const result of ['../foreign.json', '/foreign.json', 'G:/foreign.json', 'nested/../../foreign.json', 'nested\\foreign.json', 'nested//foreign.json']) {
    assert.throws(() => assertOutputContract({ ...adapter, resultFiles: [result] }));
  }
});

test('original runner rejects non-string/control argv and unsafe PowerShell arguments without running them', () => {
  const original = inventory()[0];
  for (const arg of [null, 1, {}, 'line\nflag', 'line\rflag', 'nul\0flag']) {
    assert.throws(() => validateAdapters([{ ...original, args: [arg] }], repoRoot), /ADAPTER_ARGUMENT_INVALID/);
  }
  for (const arg of ['--real-backend', '--allow-hardware', 'SetSuspendState', '-ExecutionPolicy', 'Bypass', '-Attach', '-StartApp', '-WithApp']) {
    assert.throws(() => validateAdapters([{ ...original, kind: 'powershell', args: [arg] }], repoRoot), /ADAPTER_UNSAFE_ARGUMENT/);
  }
});

test('reviewed machine, external-fixture and shared-output exclusions stay out of the default allowlist', () => {
  const admitted = new Set(inventory().map(adapter => adapter.path));
  assert(Object.isFrozen(ADAPTER_EXCLUSIONS));
  for (const exclusion of ADAPTER_EXCLUSIONS) {
    assertRepoFile(exclusion.path);
    assert.equal(typeof exclusion.reason, 'string');
    assert(exclusion.reason.length > 30, exclusion.path);
    assert(!admitted.has(exclusion.path), exclusion.path);
  }
});

test('settings regression installs a fail-closed responder before importing the actual settings owner', () => {
  const source = fs.readFileSync(path.join(repoRoot, 'tools/settings_reactivity_selftest.ts'), 'utf8');
  const start = source.indexOf('(globalThis as any).__mockIpcResponder =');
  const end = source.indexOf("const repo = await import('../src/bridge/settingsRepository')");
  assert(start >= 0 && end > start);
  const responder = source.slice(start, end);
  assert.match(responder, /throw new Error\('Forbidden non-fixture IPC:/);
  assert.doesNotMatch(responder, /\breturn\s+undefined\b/);
  assert(source.indexOf('__mockShellDryRun = true') < start);
});

test('all TypeScript tests bundle to ESM without execution and fingerprint their repository import graph', async t => {
  const esbuild = require('esbuild'), authority = lockBundledNodeTools(repoRoot);
  try {
    for (const adapter of inventory().filter(adapter => adapter.kind === 'typescript')) {
      await t.test(adapter.id, async () => {
        const result = await esbuild.build({
          absWorkingDir: repoRoot, entryPoints: [adapter.path], bundle: true, write: false,
          format: 'esm', platform: 'node', target: 'node18', metafile: true, logLevel: 'silent',
        });
        assert(result.outputFiles.length === 1, adapter.id);
        assert(result.outputFiles[0].text.length > 0, adapter.id);
        const recorded = new Set([adapter.path, ...adapter.evidenceInputs]);
        for (const input of Object.keys(result.metafile.inputs)) {
          const file = path.resolve(repoRoot, input), observed = classifyBundledInput(repoRoot, file, fs.readFileSync(file), authority);
          if (observed.role === 'tool') continue;
          assert(recorded.has(observed.row.path), `${adapter.id}: imported source needs review/fingerprinting: ${observed.row.path}`);
        }
      });
    }
  } finally {
    esbuild.stop();
    verifyBundledToolScope(authority, repoRoot);
  }
});
