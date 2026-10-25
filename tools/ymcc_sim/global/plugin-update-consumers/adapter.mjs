import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
export const MODULE='tools/ymcc_sim/global/plugin-update-consumers';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const SPECS=JSON.parse(fs.readFileSync(new URL('./source-inputs.json',import.meta.url),'utf8')).cases;
export const MODULE_INPUTS=['adapter.mjs','run.mjs','observe-build.cjs','source-inputs.json','toolchain-inputs.json','source-inputs.historical.json','retired-channel.mjs'].map(name=>MODULE+'/'+name);
export function getPluginUpdateConsumerAdapters(repo=REPO){
 let files=[],problem;
 try{const s=JSON.parse(fs.readFileSync(path.join(repo,MODULE,'toolchain-inputs.json'),'utf8'));if(s.schemaVersion!==1||!Array.isArray(s.files)||s.files.length!==5||s.files.some(x=>!path.isAbsolute(x.path)||!/^[a-f0-9]{64}$/.test(x.sha256)))throw Error();files=s.files.map(x=>x.path);}
 catch{problem='PLUGIN_UPDATE_TOOLCHAIN_PROFILE_INVALID';}
 return Object.entries(SPECS).map(([id,s])=>({id,domain:'plugins',title:s.title??(id.includes('service')?'Actual updater service/client: admission, receipts and duplicate requests':'Retired native updater contract with actual YMCC-only replacement lineage'),kind:'node',path:MODULE+'/run.mjs',args:['--case',id,'--evidence-dir','{outDir}/plugin-update'],timeoutMs:90000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['plugin-update/result.json'],evidenceInputs:[...new Set([...MODULE_INPUTS,...s.sources])],artifactInputs:files,...((problem||s.retirement)?{blockedReason:problem??s.retirement.code,blockedExitCodes:[10]}:{}),boundary:'Original production consumers with inert transports/actions only; no download, install, Steam UI, user storage or device operation.'}));
}
