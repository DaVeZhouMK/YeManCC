import path from 'node:path';
import {MODULE,HERE,FILES,AUTHORITY_FILES,spec,authority} from './contract.mjs';
export function getAdapter(repoRoot=path.resolve(HERE,'../../../..'),options={}){
 const a=authority(repoRoot,options);
 return {id:spec.id,domain:'lifecycle',title:'Original tray resident preference transaction: 11 checks and exact opposite-state assertion control',kind:'node',path:MODULE+'/output-evidence.mjs',args:['--repo',path.resolve(repoRoot),'--evidence-dir','{outDir}/tray-resident'],profiles:['desktop','full'],scope:'behavioral-mock',timeoutMs:45000,evidenceInputs:[...FILES.map(f=>MODULE+'/'+f),...AUTHORITY_FILES,...spec.sourceInputs],artifactInputs:a.tools.map(r=>r.path),resultFiles:['tray-resident/result.json','tray-resident/output-evidence.json'],active:true,joint:false,cpuPower:null,boundary:'Real TypeScript compiled with original bound esbuild and executed by the original producer VM with its original memory require fixture. Native main.cpp source contract only. No physical tray, Host, installed app, settings, or whole-app lifecycle acceptance.'};
}
export const getTrayResidentConsumerAdapter=getAdapter;

export function getTrayResidentConsumerAdapters(repoRoot=path.resolve(HERE,'../../../..'),options={}){return [getAdapter(repoRoot,options)];}
