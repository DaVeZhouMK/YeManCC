import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
export const MODULE='tools/ymcc_sim/global/controller-msi-late-hide-pending';
export const ID='controller.msi-late-hide-pending';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const extensions=/\.(?:h|hpp|hxx|c|cc|cpp|inc|inl)$/i;
function walk(repo,relative,accept){const out=[];for(const e of fs.readdirSync(path.join(repo,relative),{withFileTypes:true})){const rel=relative+'/'+e.name;if(e.isSymbolicLink())throw Error('Unexpected source link: '+rel);if(e.isDirectory())out.push(...walk(repo,rel,accept));else if(e.isFile()&&accept(e.name))out.push(rel);}return out;}
export function sourceInputs(repo=REPO){
 const files=new Set(['tools/input_coordinator_lifecycle_selftest.mjs','tools/msi_late_hide_pending_regression.h','tools/ymcc_sim/global/extra-adapters.mjs',...['adapter.mjs','run.mjs','README.md','toolchain-inputs.json'].map(n=>MODULE+'/'+n)]);
 for(const root of ['native','capabilities','deps/json','deps/webview2/build/native/include'])for(const rel of walk(repo,root,n=>extensions.test(n)))files.add(rel);
 for(const rel of walk(repo,'tools/fixtures/input_coordinator_lifecycle',n=>/\.(?:mjs|cpp|inc|h)$/i.test(n)))files.add(rel);
 // Use the existing suite's explicitly named tool inputs; no copied consumer.
 const runner=fs.readFileSync(path.join(repo,'tools/input_coordinator_lifecycle_selftest.mjs'),'utf8');
 for(const m of runner.matchAll(/['"](tools\/[^'"\r\n]+\.(?:mjs|cpp|inc|h))['"]/g))if(fs.existsSync(path.join(repo,m[1])))files.add(m[1]);
 return [...files].sort();
}
export function artifactInputs(repo=REPO){const spec=JSON.parse(fs.readFileSync(path.join(repo,MODULE,'toolchain-inputs.json'),'utf8'));return [...new Set([process.execPath,...spec.artifactFiles,path.join(repo,'deps/webview2/build/native/x64/WebView2LoaderStatic.lib')].map(p=>path.resolve(p)))];}
export function getMsiLateHidePendingAdapters(repo=REPO){
 let inputs;try{inputs={evidenceInputs:sourceInputs(repo),artifactInputs:artifactInputs(repo)};}catch(e){inputs={evidenceInputs:['adapter.mjs','run.mjs','README.md','toolchain-inputs.json'].map(n=>MODULE+'/'+n),artifactInputs:[],blockedReason:'LATE_HIDE_PENDING_INPUTS_UNAVAILABLE: '+e.message};}
 return [{id:ID,domain:'controller',title:'Actual late-hide worker, publication and inflight ownership: five current modes with five compiled rollbacks',kind:'node',path:MODULE+'/run.mjs',args:['--out','{outDir}/late-hide-pending','--compile-out','{outDir}/../../late-hide-tu'],profiles:['full'],timeoutMs:900000,scope:'behavioral-mock',...inputs,resultFiles:['late-hide-pending/RESULT.json'],boundary:'Existing full native TU suite, unchanged enqueue/pending/completion/worker and existing inert external ports. Combined positive pending 19/75, publication 1/7, inflight 1/8 and inflight-edges 13/33 plus actual orphaned-owner 1/3 in one EXE (35/126), and current compiled rollback controls with exactly 6/2/2/2/2 failed assertions. No historical-original claim, normal product Main, managed Host/SDK, driver, device, registry or service execution.'}];
}
