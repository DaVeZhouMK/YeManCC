import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { hash, need, sourceFile } from './contract.mjs';
export const MODULE = 'tools/ymcc_sim/global/game-library-ui-consumers';
export const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
export const SPECS = JSON.parse(fs.readFileSync(new URL('./cases.json', import.meta.url), 'utf8')).cases;
export const MODULE_INPUTS = ['adapter.mjs','run.mjs','contract.mjs','observe.cjs','cases.json','toolchain-inputs.json','README.md'].map(name => MODULE + '/' + name);
export function sourceFiles(key) { const s = SPECS[key]; need(s, 'CASE_UNKNOWN'); return [...new Set(['package.json', ...MODULE_INPUTS, s.producer, ...s.productSources])]; }
export function toolRows(repo = REPO) {
  const tool = JSON.parse(fs.readFileSync(sourceFile(repo, MODULE + '/toolchain-inputs.json'), 'utf8'));
  need(tool.schemaVersion === 1 && Array.isArray(tool.files) && tool.files.length === 3, 'EXACT_TOOL_PROFILE_REQUIRED');
  for (const row of tool.files) need(path.isAbsolute(row.path) && /^[a-f0-9]{64}$/.test(row.sha256) && hash(row.path) === row.sha256, 'TOOL_PROFILE_CHANGED');
  return tool;
}
export function getGameLibraryUiConsumerAdapters(repo = REPO) {
  let artifacts = [], problem;
  try { artifacts = toolRows(repo).files.map(row => row.path); } catch (error) { problem = 'LIBRARY_CONSUMER_TOOL_UNAVAILABLE: ' + error.message; }
  return Object.entries(SPECS).map(([key, spec]) => ({ id: spec.id, domain: 'game-library', title: spec.title, kind: 'node', path: MODULE + '/run.mjs', args: ['--case', key, '--evidence-dir', '{outDir}/library-consumer'], timeoutMs: 90000, profiles: ['desktop','full'], scope: 'behavioral-mock', evidenceInputs: sourceFiles(key), artifactInputs: artifacts, resultFiles: ['library-consumer/result.json'], ...(problem ? { blockedReason: problem } : {}), boundary: 'Unchanged existing JS extraction consumer and all original named cases execute the actual workspace UI functions. Fresh source-identical owned views; one exact function fault per family. Native bridge/Steam/account discovery/filesystem endpoints are original memory ports. No actual Steam, network, installation, hardware, browser navigation or whole-page acceptance.' }));
}