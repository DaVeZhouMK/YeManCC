import {lightingSourceInputs} from './index.mjs';
// The parent chat owns registration. Import or copy this descriptor there.
export const lightingAdapter = {
  id: 'lighting.product-signals', domain: 'lighting', title: 'Lighting product consumers, receipt and identity contract with safe HID replay',
  kind: 'node', path: 'tools/ymcc_sim/global/lighting-signals/index.mjs',
  args: ['--evidence-dir', '{outDir}/lighting-signals'], timeoutMs: 300000,
  profiles: ['desktop', 'full'], scope: 'behavioral-mock', resultFiles: ['lighting-signals/result.json'],
  evidenceInputs: [...lightingSourceInputs],
};
