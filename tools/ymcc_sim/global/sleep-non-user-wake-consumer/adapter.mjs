import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
export const MODULE='tools/ymcc_sim/global/sleep-non-user-wake-consumer';
export const CASE_ID='sleep.non-user-wake-consumer';
// Match exact optional language-input selection to the owned compiler process.
export const COMPILER_DIAGNOSTIC_ENVIRONMENT=Object.freeze({VSLANG:'1033'});
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const moduleSources=()=>['adapter.mjs','run.mjs','worker.mjs','sitecustomize.py','compiler-leaf.mjs','joined-build-reference.mjs','boundary-probe.cpp','spec.json','toolchain-inputs.json','runner.test.mjs','README.md','sender-current-continuity.json'].map(x=>MODULE+'/'+x);
export function toolchainSpec(repo=REPO){
 const own=JSON.parse(fs.readFileSync(path.join(repo,MODULE,'toolchain-inputs.json'),'utf8'));
 let sharedBytes=fs.readFileSync(path.join(repo,own.sharedManifest));
 const observedSharedManifestSha256=createHash('sha256').update(sharedBytes).digest('hex');
 if(observedSharedManifestSha256!==own.sharedManifestSha256){
  const successor=own.sharedManifestSuccessor;
  if(successor?.schemaVersion!==1||successor.kind!=='exact-shared-manifest-predecessor'||
     successor.currentSha256!==observedSharedManifestSha256||successor.historicalSha256!==own.sharedManifestSha256||
     successor.historicalAcceptanceRenewed!==false||typeof successor.historicalBase64!=='string')
   throw Error('NONUSER_SHARED_TOOL_MANIFEST_CHANGED');
  const historical=Buffer.from(successor.historicalBase64,'base64');
  if(createHash('sha256').update(historical).digest('hex')!==own.sharedManifestSha256)
   throw Error('NONUSER_SHARED_TOOL_PREDECESSOR_CHANGED');
  sharedBytes=historical;
 }
 const shared=JSON.parse(sharedBytes.toString('utf8'));
 const key=p=>path.resolve(p).toLowerCase(),inherited=new Map(),combined=new Map(),omitted=[];
 for(const row of shared.files){const k=key(row.path);if(inherited.has(k)&&inherited.get(k).sha256!==row.sha256)throw Error('NONUSER_INHERITED_TOOL_HASH_CONFLICT:'+row.path);inherited.set(k,row);}
 for(const row of [...shared.files,...own.additional]){const k=key(row.path);if(combined.has(k)&&combined.get(k).sha256!==row.sha256)throw Error('NONUSER_TOOL_HASH_CONFLICT:'+row.path);combined.set(k,row);}
 const reviewed=new Set();
 for(const row of own.reviewedAbsentOptionalInputs??[]){
  const k=key(row.path),original=inherited.get(k);
  if(reviewed.has(k)||!original||original.sha256!==row.sha256||!row.reason||row.reviewedState!=='absent')throw Error('NONUSER_OPTIONAL_TOOL_REVIEW_MISMATCH:'+row.path);
  reviewed.add(k);
  // A restored file remains in the original hash preflight. Never drop an
  // arbitrary missing file, or hide access errors as proof of absence.
  let absent=false;try{fs.statSync(row.path);}catch(error){if(error.code!=='ENOENT')throw error;absent=true;}
  if(absent&&(!row.requiredVSLANG||COMPILER_DIAGNOSTIC_ENVIRONMENT.VSLANG===row.requiredVSLANG)){combined.delete(k);omitted.push(row);}
 }
 // An explicit current-tool review keeps every original pin in the manifest.
 // Unknown/missing/changed inputs outside these exact tuples still refuse.
 const overrides=new Set();
 for(const row of own.reviewedCurrentToolOverrides??[]){
  const k=key(row.path),original=combined.get(k);
  if(overrides.has(k)||!original||original.sha256!==row.priorSha256||
     !/^[a-f0-9]{64}$/.test(row.sha256)||row.reviewScope!=='current-installed-software-validation-tool-only')
   throw Error('NONUSER_CURRENT_TOOL_REVIEW_MISMATCH:'+row.path);
  overrides.add(k);combined.set(k,{...original,sha256:row.sha256,priorSha256:row.priorSha256});
 }
 const files=[...combined.values()];
 return {...shared,files,compilerDiagnosticEnvironment:COMPILER_DIAGNOSTIC_ENVIRONMENT,sharedManifest:own.sharedManifest,sharedManifestSha256:observedSharedManifestSha256,historicalSharedManifestSha256:own.sharedManifestSha256,additionalInputsMeaning:own.initialMeaning,reviewedAbsentOptionalInputs:own.reviewedAbsentOptionalInputs??[],omittedReviewedOptionalInputs:omitted};
}
export function inputFiles(repo=REPO){const spec=JSON.parse(fs.readFileSync(path.join(repo,MODULE,'spec.json'),'utf8')),inputSuccessor=spec.publicWakeSourceReview?.inputSuccessor;return [...new Set([...moduleSources(),...spec.productSources,...spec.sharedLeaves,...(inputSuccessor?[inputSuccessor.manifest]:[])])];}
export function getSleepNonUserWakeConsumerAdapters(repo=REPO){return [{id:CASE_ID,domain:'sleep',title:'Original non-user wake/retry native consumers: S3/unknown refusal,120-second gate and four pinned event replays',kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/non-user-wake'],timeoutMs:180000,profiles:['desktop','full','sleep'],scope:'behavioral-mock',resultFiles:['non-user-wake/result.json'],evidenceInputs:inputFiles(repo),artifactInputs:toolchainSpec(repo).files.map(x=>x.path),boundary:'Original native retry/wake handlers, shared cumulative budget and actual pure fan retryBoundary state. Original S3/unknown/120-second/three original JSON assertions plus current delayed-entry replay stay intact; SetSuspendState/PID/clock/ports use enforced memory boundaries. Real OS S0/S3/S4,USB4,Host or physical resume not accepted.'}];}

// The existing native run declarations are the sole joined selection catalog.
export function getJoinedSenderChoices(repo=REPO){
 const source=fs.readFileSync(path.join(repo,'tools/fixtures/sleep_non_user_wake/resume_sender_join.inc'),'utf8');
 const choices=[...source.matchAll(/^\s*run\("([a-z0-9-]+)",/gm)].map(match=>match[1]);
 if(choices.length!==133||new Set(choices).size!==choices.length)throw Error('NONUSER_JOINED_CASE_CATALOG_CHANGED');
 return choices;
}
export function getJoinedSenderSelection(repo=REPO,value=null){
 const choices=getJoinedSenderChoices(repo);
 if(value===null)return {requested:null,selectors:choices};
 if(typeof value!=='string'||!value.length)throw Error('NONUSER_JOINED_SELECTION_UNKNOWN');
 const suffix=value.startsWith('from:'),exact=suffix?value.slice(5):value,index=choices.indexOf(exact);
 if(index<0)throw Error('NONUSER_JOINED_SELECTION_UNKNOWN');
 return {requested:value,selectors:suffix?choices.slice(index):[exact]};
}
