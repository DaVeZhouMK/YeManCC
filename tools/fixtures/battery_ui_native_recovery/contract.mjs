import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {resolveSnapshotNodeTools,mapBoundNodeRows,validateBoundNodeDeclaration} from './snapshot-node-binding.mjs';
import {sourceFiles as originalSources, hash, digest, read, write, need, inside, walk} from '../battery_native_host_roundtrip/contract.mjs';
export {hash,digest,read,write,need,inside,walk};
export const FIXTURE='tools/fixtures/battery_ui_native_recovery';
export const MODULE='tools/ymcc_sim/global/battery-ui-native-recovery-consumer';
export const CASE_ID='battery.ui-native-recovery-consumer';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
export const INPUT_LIMIT=3072;
const consumers=['tools/ymcc_sim/global/snapshot-node-tools.mjs','tools/ymcc_sim/global/source-snapshot.mjs','package.json','src/styles/tokens.css','src/assets/mouse-classic.png','src/components/BatteryChargeSettings.vue','src/components/Dropdown.vue','src/components/InlineIcon.vue','src/components/AppIcon.vue','src/bridge/batteryCharge.ts','src/bridge/uiLifecycle.ts','src/robust/frontendDiagnostics.ts','src/robust/repairModel.ts','src/version.ts','src/icons.ts','src/icons.json'];
export function sourceFiles(repo=REPO){
 return [...new Set([...originalSources(repo),...consumers,...[FIXTURE,MODULE].flatMap(dir=>walk(path.join(repo,dir)).map(p=>path.relative(repo,p).split(path.sep).join('/')))])].sort();
}
export function validateMembers(rows){
 need(Array.isArray(rows)&&rows.length>2375&&rows.length<=INPUT_LIMIT,'BATTERY_UI_COMPLETE_INPUT_BUDGET');
 need(new Set(rows.map(x=>path.resolve(x.path).toLowerCase())).size===rows.length,'BATTERY_UI_DUPLICATE_INPUT');
 need(rows.every(x=>typeof x.path==='string'&&path.isAbsolute(x.path)&&/^[a-f0-9]{64}$/.test(x.sha256)),'BATTERY_UI_INPUT_SCHEMA');
 return rows;
}
export function mergeCurrentSourceArtifact(byPath,{repo,relative,authority}){
 const p=path.join(repo,relative),key=path.resolve(p).toLowerCase(),sha256=hash(p),old=byPath.get(key);
 const sourceRole='current captured source artifact; rebuilt outputs are separately bound';
 if(!old){byPath.set(key,{path:p,sha256,role:sourceRole});return;}
 // An immutable capture verifier can be both captured source and the verified
 // tool authority. Keep one exact path/hash row with both roles; no dependency
 // is removed and arbitrary source/tool collisions still reject.
 const verifier=path.resolve(repo,'tools/ymcc_sim/global/source-snapshot.mjs').toLowerCase();
 const declared=authority?.metadata.find(row=>path.resolve(row.path).toLowerCase()===key);
 need(key===verifier&&declared,'BATTERY_UI_SOURCE_TOOL_DUPLICATE');
 need(old.sha256===sha256&&declared.sha256===sha256,'BATTERY_UI_SOURCE_TOOL_HASH_CONFLICT');
 byPath.set(key,{...old,sourceArtifact:{path:p,sha256,role:sourceRole}});
}
export function toolchain(repo=REPO,nodeBinding={}){
 const spec=read(path.join(repo,FIXTURE,'toolchain-inputs.json'));
 need(spec.externalToolFiles===2362&&spec.files.length===2362&&spec.artifactInputLimit===INPUT_LIMIT,'BATTERY_UI_EXTERNAL_CLOSURE_CHANGED');
 const authority=resolveSnapshotNodeTools(repo,nodeBinding);
 const files=authority?mapBoundNodeRows(spec.files,authority):spec.files.map(x=>({...x,path:x.repoRelative?path.resolve(repo,x.repoRelative):path.resolve(x.path)}));
 if(!authority)for(const row of files)if(row.repoRelative)need(inside(repo,row.path),'BATTERY_UI_TOOL_REPO_ESCAPE');
 const byPath=new Map(files.map(x=>[path.resolve(x.path).toLowerCase(),x]));need(byPath.size===spec.files.length,'BATTERY_UI_DUPLICATE_EXTERNAL_TOOL');
 if(authority)for(const row of [...authority.files,...authority.metadata]){
  const key=path.resolve(row.path).toLowerCase(),old=byPath.get(key);need(!old||old.sha256===row.sha256,'BATTERY_UI_BOUND_NODE_HASH_CONFLICT');
  if(!old)byPath.set(key,row);
 }
 const externalCount=byPath.size;
 const original=read(path.join(repo,'tools/fixtures/battery_native_host_roundtrip/toolchain-inputs.json'));
 need(original.files.length===938,'BATTERY_UI_ORIGINAL_NATIVE_CLOSURE_CHANGED');
 for(const x of original.files)need(byPath.get(path.resolve(x.path).toLowerCase())?.sha256===x.sha256,'BATTERY_UI_ORIGINAL_TOOL_OMITTED:'+x.path);
 for(const relative of sourceFiles(repo))mergeCurrentSourceArtifact(byPath,{repo,relative,authority});
 return {...spec,files:validateMembers([...byPath.values()].sort((a,b)=>a.path.localeCompare(b.path))),externalCount,originalExternalCount:2362,currentSourceArtifactCount:sourceFiles(repo).length,...(authority?{nodePackageRepo:authority.packageRepo,nodeModulesPath:authority.nodeModulesPath,esbuildBinary:authority.esbuildBinary,nodeToolBinding:authority.proof}:{})};
}
export const artifactFiles=(repo=REPO,nodeBinding={})=>toolchain(repo,nodeBinding).files.map(x=>x.path);
export function verifyTools(tool){validateMembers(tool.files);if(tool.nodeToolBinding){const authority=resolveSnapshotNodeTools(tool.nodeToolBinding.snapshotRoot,tool.nodeToolBinding);need(tool.nodePackageRepo===authority.packageRepo&&tool.nodeModulesPath===authority.nodeModulesPath&&tool.esbuildBinary===authority.esbuildBinary,'BATTERY_UI_NODE_BUILD_CONFIG_CHANGED');validateBoundNodeDeclaration(tool.files,authority);}for(const x of tool.files)need(fs.existsSync(x.path)&&hash(x.path)===x.sha256,'BATTERY_UI_TOOL_INPUT_CHANGED:'+x.path);}
export function checkPlan(plan){
 need(plan.positiveCases===3&&plan.sourceFaults===1&&JSON.stringify(plan.cases)===JSON.stringify(['structured-native-failure','throw-control','fresh-unknown-control']),'BATTERY_UI_FIXED_CASES');
 need(plan.expectedPositiveExit===0&&plan.fault?.expectedExit===23&&plan.fault.name==='software-stop-fallback-removed'&&JSON.stringify(plan.fault.expectedFailed)===JSON.stringify([{case:'structured-native-failure',assertion:'software-owned-stop-control-survives-native-structured-failure'}]),'BATTERY_UI_FIXED_SOURCE_FAULT');
 need(plan.explicitUiReleaseOnly===true&&plan.automaticSetReplay===false,'BATTERY_UI_NO_AUTOMATIC_MUTATION');return plan;
}
export function stopFallbackFault(source){
 const before='return mayRunSoftware.value && !items.some(option => option.value === 100)',after='return false && !items.some(option => option.value === 100)';
 need(source.split(before).length===2&&source.includes("? [...items, { value: 100, label: '100%' }] : items;"),'BATTERY_UI_STOP_FALLBACK_ANCHOR_CHANGED');
 return {source:source.replace(before,after),before,after,beforeSha256:digest(source),afterSha256:digest(source.replace(before,after))};
}
export function verifyOwnedJob(receipt,{nonce,root,expectedExit}){
 need(receipt?.nonce===nonce&&path.resolve(receipt.root)===path.resolve(root)&&Number.isInteger(receipt.workerPid)&&receipt.workerPid>0&&receipt.workerExitCode===expectedExit&&receipt.status===(expectedExit===0?'PASS':'FAIL')&&receipt.natural===true&&receipt.forcedTermination===false&&receipt.jobAssignedBeforeGate===true,'BATTERY_UI_OWNED_JOB_TERMINAL');return receipt;
}
