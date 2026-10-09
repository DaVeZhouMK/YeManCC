/** No product files are edited: mutate only esbuild's in-memory load of one module. */
import { build } from 'esbuild';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
const root = process.cwd();
const output = path.resolve(process.env.FAN943_MUTATION_OUTPUT ?? '../../Build/Validation/Fan943Mutations');
mkdirSync(output, { recursive: true });
const bridge = path.resolve('src/bridge/fanHost.ts');
const policy = path.resolve('src/bridge/fanSessionEvidence.ts');
const original = readFileSync(bridge, 'utf8').replace(/\r\n/g, '\n');
const originalPolicy = readFileSync(policy, 'utf8').replace(/\r\n/g, '\n');
const replace = (source, a, b) => { if (!source.includes(a)) throw new Error('mutation anchor absent: ' + a); return source.replace(a, b); };
const mutants = [
  ['stale-flags', bridge, replace(original, "if (evidence.freshness === 'stale' || evidence.freshness === 'future' || evidence.freshness === 'invalid') {", 'if (false) {')],
  ['future-ready', bridge, replace(original, "const mismatchedGeneration = evidence.freshness === 'stale' || evidence.freshness === 'future';", "const mismatchedGeneration = evidence.freshness === 'stale';")],
  ['explicit-false-ready', bridge, replace(replace(original, "if (remote.controlAccepting === false) return 'waiting';", '// mutant ignores explicit false'), "if (remote.controlAccepting === undefined && (phase === 'Ready' || phase === 'AwaitingControl')", "if ((remote.controlAccepting === undefined || remote.controlAccepting === false) && (phase === 'Ready' || phase === 'AwaitingControl')")],
  ['fresh-open-proof-removed', bridge, replace(original, '    this.explicitOpenBinding = { sessionGeneration: this.sessionGeneration, powerGeneration: generation };', '    // mutant discards completed explicit HC Open proof')],
  ['lease-cleared-dereference', bridge, replace(original, 'this.adapter.releaseControl(cleanupLeaseId)', 'this.adapter.releaseControl(this.lease.leaseId)')],
  ['old-rearm-shared-with-new-intent', bridge, replace(original, 'if (existing.intentRevision === intentRevision && existing.sessionGeneration === sessionGeneration) return existing.promise;', 'return existing.promise;')],
  ['positive-receipt-borrows-zero-proof', policy, replace(originalPolicy, 'if (generation === 0 && isNoPowerOperationReceipt(remote) && isCompleteAdmissibleHcSession(remote)) {', 'if (isCompleteAdmissibleHcSession(remote)) {')],
];
const results = [];
for (const [name, changedPath, contents] of mutants) {
  const outfile = path.join(output, name + '.mjs');
  await build({ entryPoints: ['tools/fan_session_evidence_selftest.ts'], bundle: true, platform: 'node', format: 'esm', outfile,
    alias: { '@': path.join(root, 'src') }, logLevel: 'silent', plugins: [{ name: 'memory-only-fan-mutant', setup(b) {
      b.onLoad({ filter: /fan(?:Host|SessionEvidence)\.ts$/ }, args => path.resolve(args.path) === changedPath ? { contents, loader: 'ts' } : undefined);
    } }] });
  const run = spawnSync(process.execPath, [outfile], { cwd: root, windowsHide: true, encoding: 'utf8', timeout: 30000 });
  writeFileSync(path.join(output, name + '.out'), (run.stdout ?? '') + (run.stderr ?? ''));
  const killed = run.status !== null && run.status !== 0;
  results.push({ name, exit: run.status, detected: killed, output: path.join(output, name + '.out'), hardwareCalls: 0 });
  console.log(name + ': ' + (killed ? 'DETECTED' : 'NOT_DETECTED'));
  if (!killed) throw new Error('regression did not detect ' + name);
}
writeFileSync(path.join(output, 'mutation-results.json'), JSON.stringify({ ok: true, count: results.length, productFilesModified: false, results }, null, 2));
console.log('FAN943_MUTATION_PASS count=' + results.length);
