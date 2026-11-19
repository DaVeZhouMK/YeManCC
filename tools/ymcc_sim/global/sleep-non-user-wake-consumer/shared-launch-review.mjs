import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import {createHash} from 'node:crypto';import {reverseReviewedDelta} from '../sleep-native-recovery-consumer/script-review.mjs';
const digest=b=>createHash('sha256').update(b).digest('hex'),need=(v,m)=>{if(!v)throw Error(m);},key=p=>path.resolve(p).toLowerCase();
export function verifySharedLaunchLeaves(repo,spec){
 const binding=spec.sharedLaunchCompatibilityReview;
 if(!binding)return {sha256For:relative=>digest(fs.readFileSync(path.join(repo,relative))),verifyOriginalSourceReview:original=>original(repo),receipt:null};
 need(binding.manifest==='tools/ymcc_sim/global/sleep-non-user-wake-consumer/shared-launch-review.json'&&binding.sha256==='d5fc8f4cfc42ca1cabfdbecbb28ad381d3ac88635ec879303a3efc9e7a530d90','SHARED_LAUNCH_REVIEW_BINDING_CHANGED');
 const bytes=fs.readFileSync(path.join(repo,binding.manifest));need(digest(bytes)===binding.sha256,'SHARED_LAUNCH_REVIEW_MANIFEST_CHANGED');const review=JSON.parse(bytes);
 need(review.schemaVersion===1&&review.kind==='exact-public-shared-launch-successor'&&review.sourceApproval===false&&review.defaultParentApprovalRenewed===false&&review.deltas.length===2,'SHARED_LAUNCH_REVIEW_SCHEMA');
 const allowed=new Set(['tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/sleep-native-recovery-consumer/worker.ps1']),prior=new Map(),observed=[];
 for(const row of review.deltas){need(allowed.delete(row.path),'SHARED_LAUNCH_REVIEW_SOURCE_SET');const current=fs.readFileSync(path.join(repo,row.path)),restored=reverseReviewedDelta(current,row);prior.set(key(path.join(repo,row.path)),restored);const declared=spec.reviewedSharedLeafIdentities.find(x=>x.path===row.path);need(declared?.sha256===row.beforeSha256,'SHARED_LAUNCH_REVIEW_PRIOR_IDENTITY');observed.push({path:row.path,currentSha256:digest(current),restoredSha256:digest(restored)});}
 need(allowed.size===0,'SHARED_LAUNCH_REVIEW_SOURCE_SET');
 const read=(file,...args)=>prior.get(key(file))??fs.readFileSync(file,...args);
 return {sha256For:relative=>digest(read(path.join(repo,relative))),verifyOriginalSourceReview(original){const exact=original.toString();need(digest(exact)===review.originalSourceReviewFunctionSha256,'SHARED_LAUNCH_ORIGINAL_REVIEW_FUNCTION_CHANGED');const originalWithReader=vm.compileFunction('return ('+exact+');',['fs','path','createHash'])({...fs,readFileSync:read},path,createHash);return {...originalWithReader(repo),sharedLaunchSuccessor:{manifest:binding.manifest,sha256:binding.sha256,exactOriginalFunctionSha256:digest(exact),onlySourceReaderAdapted:true}};},receipt:{manifest:binding.manifest,sha256:binding.sha256,observed,oldIdentitiesRetained:true,originalRefusalsRetained:true,sourceApproval:false,defaultParentApprovalRenewed:false}};
}
