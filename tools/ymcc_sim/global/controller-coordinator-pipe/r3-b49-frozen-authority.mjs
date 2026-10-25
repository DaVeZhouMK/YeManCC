import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {verifyCapturedSourceSnapshot} from './historical/source-snapshot-v6.mjs';

// A deliberately version-specific alternative to the unchanged creation-PASS path.
export const B49_FROZEN_PINS=Object.freeze({
 manifestSha256:'ab62c889ae8c71a3ca0f14600ab4a5973bb438ad4ccb0db5713ce3e5704d855c',
 bindingSha256:'ff2a4b96c8df2aed66cc3688370e47cee6122ef7d05b6b2d385821b1101307f6',
 verifierSha256:'f0d497ac9f639c878baa32c0fdf8b3e20e09b999fec510128583a6b8bf7b9877',
 mainSha256:'49d0fa97671a13c3116c75ae08dc54540470d120890f4550fc170bb2ebd9614f'
});
const verifiedAuthorities=new WeakSet();
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const need=(value,code)=>{if(!value)throw Error('R3_B49_FROZEN_'+code);};
const absolute=(value,label)=>{need(typeof value==='string'&&path.isAbsolute(value),label+'_ABSOLUTE_REQUIRED');return path.resolve(value);};
const same=(a,b)=>path.resolve(a).toLowerCase()===path.resolve(b).toLowerCase();
const identity=file=>({path:file,sha256:hash(fs.readFileSync(file))});
export function lockB49FrozenAuthority({mode='B49_FROZEN_V6',sourceRoot,manifestFile,expectedManifestSha256,fixedRunInputBinding,productMainSha256=B49_FROZEN_PINS.mainSha256}={}){
 need(mode==='B49_FROZEN_V6','EXPLICIT_MODE_REQUIRED');
 const snapshotRoot=absolute(sourceRoot,'CAPTURE_ROOT'),file=absolute(manifestFile,'MANIFEST');
 need(expectedManifestSha256===B49_FROZEN_PINS.manifestSha256,'EXACT_ORIGINAL_MANIFEST_PIN_REQUIRED');
 const manifestBytes=fs.readFileSync(file);need(hash(manifestBytes)===expectedManifestSha256,'MANIFEST_CHANGED');
 const manifest=JSON.parse(manifestBytes);
 need(manifest.status==='FAIL'&&manifest.failure?.code==='SNAPSHOT_SOURCE_OR_COPY_DRIFT','ORIGINAL_CREATION_FAIL_REQUIRED');
 need(same(snapshotRoot,manifest.snapshotRoot)&&same(file,path.join(snapshotRoot,'.ymcc-source-snapshot/manifest.json')),'CAPTURE_ROOT_DIFFERS');
 need(productMainSha256===B49_FROZEN_PINS.mainSha256,'EXACT_MAIN_PIN_REQUIRED');
 const receiptPath=absolute(fixedRunInputBinding?.path,'BINDING');
 need(fixedRunInputBinding.sha256===B49_FROZEN_PINS.bindingSha256,'EXACT_ORIGINAL_BINDING_PIN_REQUIRED');
 const bindingBytes=fs.readFileSync(receiptPath);need(hash(bindingBytes)===fixedRunInputBinding.sha256,'BINDING_CHANGED');
 const receipt=JSON.parse(bindingBytes);
 need(receipt.creationStatus===manifest.status&&receipt.captureComplete===true&&receipt.snapshotIntegrityVerified===true,'BINDING_CAPTURE_CLAIM_DIFFERS');
 need(same(receipt.snapshotRoot,snapshotRoot)&&same(receipt.manifest,file)&&receipt.manifestSha256===expectedManifestSha256&&receipt.productMainSha256===productMainSha256,'BINDING_SOURCE_DIFFERS');
 const verifierFile=fileURLToPath(new URL('./historical/source-snapshot-v6.mjs',import.meta.url));
 need(identity(verifierFile).sha256===B49_FROZEN_PINS.verifierSha256&&receipt.verifierCodeSha256===B49_FROZEN_PINS.verifierSha256,'ORIGINAL_V6_VERIFIER_CHANGED');
 // No injected verifier, summary receipt, cached PASS or creation-status rewrite.
 const proof=verifyCapturedSourceSnapshot({manifestFile:file,repoRoot:manifest.sourceRoot,expectedManifestSha256});
 need(proof.status==='FROZEN_SOURCE_VERIFIED'&&proof.snapshotIntegrityVerified===true&&proof.creationStatus===manifest.status&&proof.sourceApprovalGranted===false,'COMPLETE_V6_PROOF_REQUIRED');
 need(proof.manifestSha256===expectedManifestSha256&&same(proof.snapshotRoot,snapshotRoot)&&proof.main.captured.sha256===productMainSha256,'VERIFIED_SOURCE_DIFFERS');
 need(proof.fileCount===manifest.files.length&&proof.copyVerifiedFiles===manifest.files.length,'COMPLETE_CAPTURE_REQUIRED');
 need(hash(fs.readFileSync(file))===expectedManifestSha256&&hash(fs.readFileSync(receiptPath))===fixedRunInputBinding.sha256,'AUTHORITY_CHANGED_DURING_VERIFY');
 const authority={mode,snapshotRoot,manifest:identity(file),binding:identity(receiptPath),verifier:identity(verifierFile),creationStatus:manifest.status,sourceApprovalGranted:false,approvalScope:'captured-version-only',proof};
 verifiedAuthorities.add(authority);return authority;
}
