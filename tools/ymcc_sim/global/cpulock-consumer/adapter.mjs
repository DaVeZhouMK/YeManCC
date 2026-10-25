import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
export const MODULE = 'tools/ymcc_sim/global/cpulock-consumer';
export const CASE_ID = 'performance.cpulock-consumer';
export const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
export function moduleSources() {
  return ['adapter.mjs', 'run.mjs', 'worker.mjs', 'scenarios.mjs', 'memory-ipc.mjs', 'spec.json', 'toolchain-inputs.json', 'README.md'].map(name => MODULE + '/' + name);
}
export function getCpuLockConsumerAdapters(repo = REPO) {
  const read = name => JSON.parse(fs.readFileSync(path.join(repo, MODULE, name), 'utf8').replace(/^\uFEFF/, ''));
  const tools = read('toolchain-inputs.json');
  const sources = read('source-inputs.json');
  return [{ id: CASE_ID, domain: 'performance', title: 'Legacy CPU lock actual TS consumer: side isolation, migration, queues, commit failures and best-effort power receipts',
    kind: 'node', path: MODULE + '/run.mjs', args: ['--evidence-dir', '{outDir}/cpulock'], timeoutMs: 90000,
    profiles: ['desktop', 'full'], scope: 'behavioral-mock', resultFiles: ['cpulock/result.json'],
    evidenceInputs: [...moduleSources(), MODULE + '/source-inputs.json', ...sources.files], artifactInputs: tools.files.map(row => row.path),
    boundary: 'Actual cpulock/settingsRepository/yeman/api and real Vue toRaw, only ipc.ts is replaced by an allowlisted memory port. 13 function exports, overloads and aliases distinguished. Three private source faults must hit business assertions. No native CPU/registry/OS/Host/UI/hardware/global acceptance.' }];
}
