import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
export const MODULE='tools/ymcc_sim/global/sleep-non-user-wake-consumer';
export const CASE_ID='sleep.non-user-wake-consumer';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const moduleSources=()=>['adapter.mjs','run.mjs','worker.mjs','sitecustomize.py','compiler-leaf.mjs','boundary-probe.cpp','spec.json','toolchain-inputs.json','runner.test.mjs','README.md'].map(x=>MODULE+'/'+x);
export function toolchainSpec(repo=REPO){
 const own=JSON.parse(fs.readFileSync(path.join(repo,MODULE,'toolchain-inputs.json'),'utf8'));
 const shared=JSON.parse(fs.readFileSync(path.join(repo,own.sharedManifest),'utf8'));
 const files=[...new Map([...shared.files,...own.additional].map(x=>[path.resolve(x.path).toLowerCase(),x])).values()];
 return {...shared,files,sharedManifest:own.sharedManifest,sharedManifestSha256:own.sharedManifestSha256,additionalInputsMeaning:own.initialMeaning};
}
export function inputFiles(repo=REPO){const spec=JSON.parse(fs.readFileSync(path.join(repo,MODULE,'spec.json'),'utf8'));return [...new Set([...moduleSources(),...spec.productSources,...spec.sharedLeaves])];}
export function getSleepNonUserWakeConsumerAdapters(repo=REPO){return [{id:CASE_ID,domain:'sleep',title:'Original non-user wake/retry native consumers: S3/unknown refusal,120-second gate and four pinned event replays',kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/non-user-wake'],timeoutMs:180000,profiles:['desktop','full','sleep'],scope:'behavioral-mock',resultFiles:['non-user-wake/result.json'],evidenceInputs:inputFiles(repo),artifactInputs:toolchainSpec(repo).files.map(x=>x.path),boundary:'Original native retry/wake handlers, shared cumulative budget and actual pure fan retryBoundary state. Original S3/unknown/120-second/three original JSON assertions plus current delayed-entry replay stay intact; SetSuspendState/PID/clock/ports use enforced memory boundaries. Real OS S0/S3/S4,USB4,Host or physical resume not accepted.'}];}
