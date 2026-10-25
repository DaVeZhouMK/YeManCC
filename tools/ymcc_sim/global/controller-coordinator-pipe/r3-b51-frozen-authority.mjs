import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';import {createHash} from 'node:crypto';
import {verifyCapturedSourceSnapshot} from '../source-snapshot.mjs';
// Explicit captured B51 source authority for compile-only. No creation-PASS shortcut.
export const B51_FROZEN_PINS=Object.freeze({
 mode:'B51_FROZEN_V7_PRIVATE_COMPILE_ONLY',sourceRoot:'G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC-SimSnapshot-20261021-B51',
 manifestSha256:'3282961fc2d6473e2c4c86e889326c368cf91b1d78e35b8f41e967fe9f4ffa44',
 bindingPath:'D:/Codex/2026-10-18/kai-s/work/global-simulator-20261018/phase32/B51-fixed-run-input-bindings.json',
 bindingSha256:'adda6d41dc82a2a99fdd709d50ddb5b477a05ba7242ee5a64181013938910133',
 nodeBindingSha256:'425fe0c4c5b5b4140374cf46e5d085d7becb9e7f87e18f791b4c48d6d9b88002',
 verifierSha256:'5098f2b8a1a221ec8b4940fdc22da6e8b2d2fd171729719cfea2cf819493ba6a',
 mainSha256:'bd33888ac15c18eca5dac5bd94caa8387ec4bf8d6a2f4384b4f47cd90d528d3f'});
const need=(x,m)=>{if(!x)throw Error('R3_B51_FROZEN_'+m);},sha=b=>createHash('sha256').update(b).digest('hex'),same=(a,b)=>typeof a==='string'&&typeof b==='string'&&path.resolve(a).toLowerCase()===path.resolve(b).toLowerCase(),identity=file=>({path:file,sha256:sha(fs.readFileSync(file))});
export function lockB51FrozenAuthority({mode,sourceRoot,manifestFile,expectedManifestSha256,fixedRunInputBinding,productMainSha256}={}){
 const p=B51_FROZEN_PINS;need(mode===p.mode,'EXPLICIT_MODE_REQUIRED');need(same(sourceRoot,p.sourceRoot),'EXACT_B51_ROOT_REQUIRED');need(same(fs.realpathSync(sourceRoot),p.sourceRoot),'B51_ROOT_REDIRECTION');
 need(same(manifestFile,path.join(p.sourceRoot,'.ymcc-source-snapshot/manifest.json'))&&expectedManifestSha256===p.manifestSha256,'EXACT_MANIFEST_PIN_REQUIRED');need(productMainSha256===p.mainSha256,'EXACT_MAIN_PIN_REQUIRED');
 need(same(fixedRunInputBinding?.path,p.bindingPath)&&fixedRunInputBinding?.sha256===p.bindingSha256,'EXACT_FIXED_RUN_BINDING_REQUIRED');
 const mb=fs.readFileSync(manifestFile),bb=fs.readFileSync(p.bindingPath);need(sha(mb)===p.manifestSha256&&sha(bb)===p.bindingSha256,'MANIFEST_OR_BINDING_CHANGED');const manifest=JSON.parse(mb),binding=JSON.parse(bb);
 need(manifest.status==='PASS'&&manifest.policy?.version===7&&manifest.files.length===8128,'ORIGINAL_V7_CAPTURE_REQUIRED');
 need(binding.creationStatus===manifest.status&&binding.captureComplete===true&&binding.snapshotIntegrityVerified===true&&binding.sourcePolicy===7,'FIXED_CAPTURE_CLAIMS_REQUIRED');
 need(same(manifest.snapshotRoot,p.sourceRoot)&&same(binding.snapshotRoot,p.sourceRoot)&&same(binding.manifest,manifestFile)&&binding.manifestSha256===p.manifestSha256&&binding.productMainSha256===p.mainSha256,'FIXED_SOURCE_DIFFERS');
 const verifierFile=fileURLToPath(new URL('../source-snapshot.mjs',import.meta.url));need(identity(verifierFile).sha256===p.verifierSha256&&binding.verifierCodeSha256===p.verifierSha256,'EXACT_V7_VERIFIER_REQUIRED');
 const nodeBindingFile=path.join(sourceRoot,'.ymcc-source-snapshot/node-tools-binding.json');need(identity(nodeBindingFile).sha256===p.nodeBindingSha256&&binding.bindingManifestSha256===p.nodeBindingSha256&&same(binding.bindingManifest,nodeBindingFile),'EXACT_NODE_BINDING_REQUIRED');
 const proof=verifyCapturedSourceSnapshot({manifestFile,repoRoot:manifest.sourceRoot,expectedManifestSha256:p.manifestSha256});
 need(proof.status==='FROZEN_SOURCE_VERIFIED'&&proof.snapshotIntegrityVerified===true&&proof.sourceApprovalGranted===false&&proof.creationStatus==='PASS','COMPLETE_V7_FROZEN_PROOF_REQUIRED');
 need(proof.fileCount===8128&&proof.copyVerifiedFiles===8128&&proof.main.captured.sha256===p.mainSha256&&same(proof.snapshotRoot,p.sourceRoot),'EXACT_COMPLETE_CAPTURE_REQUIRED');
 need(identity(manifestFile).sha256===p.manifestSha256&&identity(p.bindingPath).sha256===p.bindingSha256&&identity(nodeBindingFile).sha256===p.nodeBindingSha256,'AUTHORITY_CHANGED_DURING_VERIFY');
 return {mode,snapshotRoot:p.sourceRoot,manifest:identity(manifestFile),binding:identity(p.bindingPath),nodeBinding:identity(nodeBindingFile),verifier:identity(verifierFile),creationStatus:'PASS',sourceApprovalGranted:false,executionApproved:false,approvalScope:'captured-B51-source-for-private-positive-compile-only',proof};
}
