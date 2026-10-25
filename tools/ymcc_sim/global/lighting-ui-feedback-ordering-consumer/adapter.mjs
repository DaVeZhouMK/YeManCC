import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

export const MODULE = 'tools/ymcc_sim/global/lighting-ui-feedback-ordering-consumer';
export const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
export const ID = 'lighting.ui-feedback-ordering-consumer';
export const HELPER = 'tools/ymcc_sim/global/lighting-signals/ui-feedback.mjs';
export const BASELINE = 'tools/controller_lighting_operations_browser_selftest.mjs';
export const COMPONENT = 'src/components/ControllerFeedbackCapabilitiesCard.vue';
export const common = () => ['adapter.mjs', 'run.mjs', 'verify.mjs', 'observe.cjs', 'Run-Helper.ps1',
  'source-inputs.json', 'tool-inputs.json', 'runner.test.mjs', 'README.md'].map(file => `${MODULE}/${file}`);
const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
export const reviewedSources = (repo = REPO) => read(path.join(repo, MODULE, 'source-inputs.json')).files;
export const toolManifest = (repo = REPO) => read(path.join(repo, MODULE, 'tool-inputs.json'));
export const reviewedTools = (repo = REPO) => toolManifest(repo).files;
export const evidenceInputs = (repo = REPO) => [...new Set([...common(), ...reviewedSources(repo),
  HELPER, BASELINE, 'package.json', 'pnpm-lock.yaml', 'tools/ymcc_sim/global/runner.mjs',
  'tools/ymcc_sim/global/host-process.ps1', 'tools/ymcc_sim/global/execution-completion/index.mjs'])].sort();
export function getLightingUiFeedbackOrderingConsumerAdapters(repo = REPO) {
  const tools = reviewedTools(repo);
  if (tools.length > 640) throw Error('LIGHTING_TOOL_CLOSURE_LIMIT');
  return [{id: ID, domain: 'lighting', title: 'Actual lighting feedback card: current receipts and rejection of historical out-of-order replies',
    kind: 'node', path: `${MODULE}/run.mjs`, args: ['--evidence-dir', '{outDir}/feedback-ordering'],
    timeoutMs: 240000, profiles: ['desktop', 'full'], scope: 'behavioral-mock',
    resultFiles: ['feedback-ordering/result.json'], evidenceInputs: evidenceInputs(repo), artifactInputs: tools}];
}
