import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
export const MODULE='tools/ymcc_sim/global/lighting-component-continuity/';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const REVIEWED_CONTRACT='deps/lightsetter-admission/candidate7b-v1/REVIEWED-CONTRACT.json';
export const REVIEWED_SHA256='92f820533b2744fe695029b301111bdb528c9d5449af6ffdeb164496a1829d93';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
function walk(root){const files=[];function visit(p){const info=fs.lstatSync(p);if(info.isSymbolicLink())throw Error('LIGHTING_REPARSE_INPUT');if(info.isFile())files.push(p);else if(info.isDirectory())for(const e of fs.readdirSync(p))visit(path.join(p,e));else throw Error('LIGHTING_NONFILE_INPUT');if(files.length>4096)throw Error('LIGHTING_INPUT_LIMIT');}visit(root);return files.sort();}
export function lightingComponentInputs(repoRoot=REPO){
 const repo=path.resolve(repoRoot),sources=[MODULE+'adapter.mjs',MODULE+'Run-Continuity.ps1',MODULE+'New-ReviewedPlan.ps1',MODULE+'Test-PortableContract.ps1','tools/hc-device-runtime/connect-installed-lighting.ps1','tools/hc-device-runtime/verify-lighting-catalog-evidence.ps1','tools/hc-device-runtime/verify-lighting-portable-evidence.ps1','tools/hc-device-runtime/Test-LightingConnection.ps1'],artifacts=[],problems=[];
 try{const file=path.join(repo,REVIEWED_CONTRACT);if(hash(file)!==REVIEWED_SHA256)throw Error('LIGHTING_REVIEWED_CONTRACT');const c=JSON.parse(fs.readFileSync(file,'utf8'));for(const name of c.requiredTools)if(!sources.includes(name))throw Error('LIGHTING_HELPER_CLOSURE');for(const name of [c.sourceBundleRepoPath,c.admissionBundleRepoPath]){if(typeof name!=='string'||path.isAbsolute(name)||name.split(/[\\/]/).includes('..'))throw Error('LIGHTING_BUNDLE_OUTSIDE_REPO');artifacts.push(...walk(path.join(repo,name)));}}
 catch(e){problems.push(e.code??e.message);artifacts.push(path.join(repo,REVIEWED_CONTRACT));}
 return {repo,sources:[...new Set(sources)],artifacts:[...new Set(artifacts)],problems};
}
export function getLightingComponentContinuityAdapter(repoRoot=REPO){const i=lightingComponentInputs(repoRoot);return{id:'lifecycle.lighting-component-continuity',domain:'lifecycle',title:'Reviewed portable lighting source/profile/payload admission and original copied-install rollback consumers',kind:'powershell',path:MODULE+'Run-Continuity.ps1',args:['-EvidenceDir','{outDir}/p','-ReviewedContractPath',path.join(i.repo,REVIEWED_CONTRACT)],profiles:['desktop','full'],scope:'behavioral-mock',timeoutMs:300000,resultFiles:['p/result.json'],evidenceInputs:i.sources,artifactInputs:i.artifacts,referenceInputs:['FanLab/LightSetter/YeManLightSetter.csproj','deps/ymcc-device-core-runtime/profile.json'],referenceMeaning:'Developing sources and profile are lineage references; this case consumes the exact reviewed immutable source/evidence/profile tuple. It never changes or launches C.'};}
export function getLightingComponentContinuityAdapters(repoRoot=REPO){return[getLightingComponentContinuityAdapter(repoRoot)];}