import {lockB51FrozenAuthority,B51_FROZEN_PINS} from './r3-b51-frozen-authority.mjs';
import {requireB49CompileBatch} from './r3-b49-compile-batch.mjs';
import {lockB49FrozenAuthority} from './r3-b49-frozen-authority.mjs';
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
export const R3_LAYOUT=JSON.parse(fs.readFileSync(new URL('./r3-prerequisite-root.json',import.meta.url),'utf8'));
const need=(v,m)=>{if(!v)throw Error('R3_EQUIVALENCE_'+m);};
export const fileHash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
export const within=(root,file)=>{const r=path.relative(path.resolve(root),path.resolve(file));return r===''||(!r.startsWith('..'+path.sep)&&r!=='..'&&!path.isAbsolute(r));};
export function verifyR3SourceAuthority(l){
 const binding=l.fixedRunInputBinding;
 if(l.sourceAuthority?.mode===B51_FROZEN_PINS.mode)return lockB51FrozenAuthority({...l.sourceAuthority,sourceRoot:l.sourceRoot,productMainSha256:l.productMainSha256,fixedRunInputBinding:binding});
 if(l.sourceAuthority?.mode==='B49_FROZEN_V6')return lockB49FrozenAuthority({...l.sourceAuthority,sourceRoot:l.sourceRoot,productMainSha256:l.productMainSha256,fixedRunInputBinding:binding});
 need(l.sourceAuthority===undefined,'UNKNOWN_SOURCE_AUTHORITY_MODE');
 need(binding&&path.isAbsolute(binding.path)&&/^[a-f0-9]{64}$/.test(binding.sha256)&&fileHash(binding.path)===binding.sha256,'FIXED_RUN_BINDING_CHANGED');
 const fixed=JSON.parse(fs.readFileSync(binding.path,'utf8'));
 need(fixed.creationStatus==='PASS'&&fixed.captureComplete===true&&fixed.snapshotIntegrityVerified===true,'UNVERIFIED_SOURCE_CAPTURE');
 need(path.isAbsolute(l.sourceRoot)&&path.resolve(l.sourceRoot)===path.resolve(fixed.snapshotRoot)&&fileHash(fixed.manifest)===fixed.manifestSha256&&fixed.productMainSha256===l.productMainSha256&&fileHash(path.join(l.sourceRoot,'native/main.cpp'))===l.productMainSha256,'FIXED_SOURCE_IDENTITY_CHANGED');
 return {mode:'ORIGINAL_CREATION_PASS',creationStatus:fixed.creationStatus};
}
function layout(){
 const l=R3_LAYOUT;
 need(typeof l.sourceVersion==='string'&&l.sourceVersion.length>0&&/^[a-f0-9]{64}$/.test(l.productMainSha256),'EXACT_SOURCE_PROFILE_REQUIRED');
 verifyR3SourceAuthority(l);
 need(path.isAbsolute(l.workspaceRoot)&&path.isAbsolute(l.authorizedBuildRoot),'DECLARED_ABSOLUTE_ROOT_REQUIRED');
 const validation=path.join(l.workspaceRoot,'Build','Validation');need(within(validation,l.authorizedBuildRoot)&&path.resolve(validation)!==path.resolve(l.authorizedBuildRoot),'OWNED_BUILD_ROOT_ONLY');
 need(fs.realpathSync(l.authorizedBuildRoot).toLowerCase()===path.resolve(l.authorizedBuildRoot).toLowerCase(),'ARTIFACT_ROOT_REDIRECTION');return l;
}
export function resolveR3ManifestPin(pin){const l=layout();need(pin&&typeof pin.manifest==='string'&&!path.isAbsolute(pin.manifest)&&!pin.manifest.split(/[\\/]/).some(s=>s==='..'||s==='.'||s==='')&&/^[a-f0-9]{64}$/.test(pin.sha256),'MANIFEST_PIN_REQUIRED');const manifest=path.resolve(l.workspaceRoot,pin.manifest);need(within(l.authorizedBuildRoot,manifest),'MANIFEST_OUTSIDE_DECLARED_BUILD');need(fileHash(manifest)===pin.sha256,'MANIFEST_IDENTITY_MISMATCH');need(within(fs.realpathSync(l.authorizedBuildRoot),fs.realpathSync(manifest)),'MANIFEST_REALPATH_ESCAPE');return manifest;}
export function verifyManagedSourceRows(value,repo,{hashFile=fileHash,batchAuthority}={}){
 const admitted=batchAuthority?requireB49CompileBatch(batchAuthority,{sourceRoot:repo,memberAdmission:true}):null;
 const rows=value?.inputBindings?.sources,expected=(admitted??R3_LAYOUT).expectedManagedSources;
 need(Array.isArray(rows)&&rows.length===20&&expected.length===20,'EXACT20_SOURCE_MEMBERS_REQUIRED');
 const reference=new Map(expected.map(r=>[r.relative,r.sha256]));need(reference.size===20,'PROFILE_MEMBERSHIP_DUPLICATE');const seen=new Set(),mapped=[];const authorizedBuildRoot=(admitted??layout()).authorizedBuildRoot;
 for(const row of rows){const rel=row.relative;need(typeof rel==='string'&&rel.length>0&&!path.isAbsolute(rel)&&!rel.includes('\\')&&!rel.includes(':')&&!rel.split('/').some(s=>s==='..'||s==='.'||s===''),'STRICT_RELATIVE_PATH_REQUIRED');need(reference.has(rel)&&!seen.has(rel),'EXACT20_SOURCE_MEMBERSHIP');seen.add(rel);const current=path.resolve(repo,rel);need(within(repo,current)&&within(fs.realpathSync(repo),fs.realpathSync(current)),'CURRENT_SOURCE_ESCAPE');need(row.sha256===reference.get(rel),'COMPILED_SOURCE_PROFILE_IDENTITY');need(path.isAbsolute(row.path)&&path.isAbsolute(row.snapshot)&&within(authorizedBuildRoot,row.snapshot),'ORIGINAL_COMPILE_OR_SNAPSHOT_SCOPE');need(hashFile(current)===row.sha256,'CURRENT_SOURCE_CHANGED:'+rel);need(hashFile(row.path)===row.sha256,'ORIGINAL_SOURCE_CHANGED:'+rel);need(hashFile(row.snapshot)===row.sha256,'SNAPSHOT_CHANGED:'+rel);mapped.push({relative:rel,currentPath:current,actualOriginalPath:row.path,actualSnapshot:row.snapshot,sha256:row.sha256});}
 need(seen.size===20,'EXACT20_SOURCE_MEMBERS_REQUIRED');return mapped;
}
export function verifyManagedBuilderAdapters(value,repo){const rows=value.inputBindings.builderAdapterInputs;need(Array.isArray(rows)&&rows.length===1,'ACTUAL_BOUND_BUILDER_ADAPTER_REQUIRED');for(const r of rows){need(r.relative==='tools/ymcc_sim/global/controller-coordinator-pipe/r3-managed-builder.mjs','BUILDER_ADAPTER_MEMBER');need(fileHash(r.path)===r.sha256&&fileHash(path.join(repo,r.relative))===r.sha256,'BUILDER_ADAPTER_CHANGED');need(fileHash(r.baselineBuilder)===r.baselineSha256&&fileHash(path.join(repo,'tools/input_lifecycle_managed_fixture.mjs'))===r.baselineSha256,'BASELINE_BUILDER_CHANGED');}return rows;}
