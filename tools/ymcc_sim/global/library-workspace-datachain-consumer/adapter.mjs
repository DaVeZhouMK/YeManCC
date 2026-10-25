import fs from 'node:fs';
import path from 'node:path';
import {MODULE,FILES,HERE,spec,shaFile} from './contract.mjs';
export function getAdapter(repoRoot=path.resolve(HERE,'../../../..')) {
  const tool = JSON.parse(fs.readFileSync(path.join(HERE,'toolchain-inputs.json'),'utf8'));
  const blockedReason = shaFile(process.execPath) === tool.node.sha256 && process.version === tool.node.version ? undefined : 'REVIEWED_NODE_IDENTITY_REQUIRED';
  return {id:'game-library.workspace-datachain-consumer',domain:'game-library',title:'Original library workspace snapshot and artwork VM datachain, all 40 cases and two semantic controls',kind:'node',path:MODULE+'/run.mjs',args:['--repo',path.resolve(repoRoot),'--evidence-dir','{outDir}/workspace-datachain'],timeoutMs:30000,profiles:['desktop','full'],scope:'behavioral-mock',evidenceInputs:[...FILES.map(f=>MODULE+'/'+f),...spec.sourceInputs],artifactInputs:[process.execPath],resultFiles:['workspace-datachain/result.json'],...(blockedReason?{blockedReason}:{}),boundary:'Original Node VM and HTML assertions; byte-identical positive producer/app/html in owned sourceview preserves original ../../Build guard. Two isolated app.js faults must reach original named assertions. No browser, native workspace80, Steam, installation, devices, or whole-global acceptance.'};
}
