import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
/** Load the exact model modules shipped in the standalone page, without its DOM. */
export function loadSimulationModels(file = 'public/performance-simulation/index.html', supportDirectory = path.dirname(file)) {
  const html=fs.readFileSync(file,'utf8'), context=vm.createContext({structuredClone,console});
  for(const name of ['gpu-capability-library.js','optimization-curve.js','ts-power-curve.js','frame-work.js','frame-pipeline.js']){const support=path.join(supportDirectory,name);if(fs.existsSync(support))vm.runInContext(fs.readFileSync(support,'utf8'),context,{filename:support});}
  for(const match of html.matchAll(/\(function\(root\)\{[\s\S]*?\}\)\(globalThis\);/g))vm.runInContext(match[0],context,{filename:file});
  return context;
}
