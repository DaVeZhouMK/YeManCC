import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {verifyReviewedOriginalScripts} from './run.mjs';
const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
const need=(ok,code)=>{if(!ok)throw Error(code);};
const reviews={pause:'a27c3cb7401a994c17ad427df4c76e68d07b9c0531a83331c1226a93221108f6',native:'d5748843edf307869eeffd984f0d1063b7d3a0aa004f9fd180e331cfaacce151'};
export function reverseReviewedDelta(bytes,row){
 need(digest(bytes)===row.afterSha256,'S4_REVIEW_CURRENT_BYTES_CHANGED:'+row.path);
 if(row.canonicalLf)bytes=Buffer.from(bytes.toString('utf8').replace(/\r\n/g,'\n'));
 const pieces=[];let cursor=0,length=0;
 for(const edit of row.edits){
  const before=Buffer.from(edit.beforeBase64,'base64'),after=Buffer.from(edit.afterBase64,'base64');
  need(Number.isSafeInteger(edit.afterOffset)&&edit.afterOffset>=cursor&&edit.afterOffset+after.length<=bytes.length,'S4_REVIEW_DELTA_RANGE');
  const unchanged=bytes.subarray(cursor,edit.afterOffset);length+=unchanged.length;
  need(edit.beforeOffset===length&&bytes.subarray(edit.afterOffset,edit.afterOffset+after.length).equals(after),'S4_REVIEW_DELTA_BYTES');
  pieces.push(unchanged,before);length+=before.length;cursor=edit.afterOffset+after.length;
 }
 pieces.push(bytes.subarray(cursor));let restored=Buffer.concat(pieces);
 if(row.canonicalLf){const lines=restored.toString('utf8').split('\n');for(const index of row.historicalCrlfLines){need(Number.isSafeInteger(index)&&index>=0&&index<lines.length-1,'S4_REVIEW_NEWLINE_RANGE');lines[index]+='\r';}restored=Buffer.from(lines.join('\n'));}
 need(digest(restored)===row.beforeSha256,'S4_REVIEW_HISTORICAL_BYTES_CHANGED:'+row.path);return restored;
}

const currentReviewHash='fad5a515e2c00fe261549832d512b5c554c1ea4b0d1b205da886d4b260cb8cac';
function verifyCurrentPowerReview(spec,readSource){
 const binding=spec.currentPowerCompatibilityReview;
 need(binding?.manifest==='tools/ymcc_sim/global/sleep-native-recovery-consumer/script-maintenance-current.json'&&binding.sha256===currentReviewHash,'CURRENT_POWER_REVIEW_CONTRACT_CHANGED');
 const bytes=readSource(binding.manifest);need(digest(bytes)===currentReviewHash,'CURRENT_POWER_REVIEW_MANIFEST_CHANGED');
 const current=JSON.parse(bytes);need(current.schemaVersion===1&&current.kind==='current-public-dispatch-and-exact-s4-history'&&current.sourceApproval===false,'CURRENT_POWER_REVIEW_SCHEMA');
 need(JSON.stringify(spec.originalScripts)===JSON.stringify(current.currentOriginalScripts),'S4_REVIEW_SCRIPT_SET_CHANGED');
 for(const row of current.currentOriginalScripts)need(digest(readSource(row.path))===row.sha256,'S4_REVIEW_CURRENT_BYTES_CHANGED:'+row.path);
 const wakePath='tools/sleep_wake_resume_selftest.py',s4Wake=reverseReviewedDelta(readSource(wakePath),current.wakeS4Delta);
 reverseReviewedDelta(readSource(wakePath),current.wakePriorLiveDelta);
 const priorBytes=readSource(current.priorS4Manifest);need(digest(priorBytes)===reviews.native&&digest(priorBytes)===current.priorS4ManifestSha256,'S4_REVIEW_MANIFEST_CHANGED');
 const review=JSON.parse(priorBytes);need(review.schemaVersion===1&&review.kind==='native'&&review.sourceApproval===false,'S4_REVIEW_SCHEMA');
 const historical=new Map(),apiPath='tools/sleep_recovery_api_selftest.py';
 const priorLiveApi=reverseReviewedDelta(readSource(apiPath),current.apiPriorLiveDelta);
 const priorApi=reverseReviewedDelta(priorLiveApi,review.feedbackRotationApiDelta);
 for(const row of review.deltas)historical.set(row.path,reverseReviewedDelta(row.path===apiPath?priorApi:s4Wake,row));
 for(const row of current.currentSourceContracts)need(digest(readSource(row.path))===row.sha256,'CURRENT_POWER_REVIEW_SOURCE_CONTRACT_CHANGED:'+row.path);
 const prior=verifyReviewedOriginalScripts(review.priorSpec,relative=>historical.get(relative)??readSource(relative));
 return {review,historical,receipt:{schemaVersion:1,status:'PASS',manifest:binding.manifest,sha256:currentReviewHash,exactReverseDeltas:true,historicalIdentitiesRetained:true,historicalS4ScriptBytesReconstructed:true,historicalS4WholeSourceApprovalRenewed:false,currentSourceContractsVerified:true,sourceApproval:false,priorReview:prior,reviewedChanges:current.reviewedChanges}};
}


const canceledLeaseReviewHash='faf961c6244a0e1fb5e89836c2cdb3ff6fe1853a2852a9251ba19b8dc55bb3bb';
function verifyCanceledLeaseReview(spec,readSource){
 const binding=spec.canceledLeaseCompatibilityReview;
 need(binding?.manifest==='tools/ymcc_sim/global/sleep-native-recovery-consumer/script-maintenance-canceled-lease.json'&&binding.sha256===canceledLeaseReviewHash,'CANCELED_LEASE_REVIEW_CONTRACT_CHANGED');
 const bytes=readSource(binding.manifest);need(digest(bytes)===canceledLeaseReviewHash,'CANCELED_LEASE_REVIEW_MANIFEST_CHANGED');
 const review=JSON.parse(bytes);
 need(review.schemaVersion===1&&review.kind==='exact-canceled-lease-current-source-pair'&&review.sourceApproval===false&&review.defaultParentApprovalRenewed===false&&review.historicalWholeSourceApprovalRenewed===false,'CANCELED_LEASE_REVIEW_SCHEMA');
 need(review.priorCurrentManifest==='tools/ymcc_sim/global/sleep-native-recovery-consumer/script-maintenance-current.json'&&review.priorCurrentManifestSha256===currentReviewHash,'CANCELED_LEASE_PRIOR_REVIEW_CHANGED');
 need(JSON.stringify(spec.originalScripts)===JSON.stringify(review.currentOriginalScripts),'CANCELED_LEASE_SCRIPT_SET_CHANGED');
 need(Array.isArray(spec.sources)&&review.currentSourceContracts.every(row=>spec.sources.includes(row.path)),'CANCELED_LEASE_SOURCE_SET_INCOMPLETE');
 for(const row of review.currentOriginalScripts)need(digest(readSource(row.path))===row.sha256,'CANCELED_LEASE_CURRENT_SCRIPT_CHANGED:'+row.path);
 const historical=new Map();
 for(const row of review.deltas){need(!historical.has(row.path),'CANCELED_LEASE_DUPLICATE_DELTA');historical.set(row.path,reverseReviewedDelta(readSource(row.path),row));}
 for(const row of review.currentSourceContracts)need(digest(readSource(row.path))===row.sha256,'CANCELED_LEASE_CURRENT_SOURCE_CHANGED:'+row.path);
 const prior=verifyCurrentPowerReview(review.priorSpec,relative=>historical.get(relative)??readSource(relative));
 const receipt={schemaVersion:1,status:'PASS',manifest:binding.manifest,sha256:canceledLeaseReviewHash,sourceApproval:false,exactReverseDeltas:true,currentSourceContractsVerified:true,legacyDefaultCasesRetained:review.legacyDefaultCasesExact,legacySelectorSetsRetained:review.legacySelectorSetsExact,historicalWholeSourceApprovalRenewed:false,defaultParentApprovalRenewed:false,priorReview:prior.receipt.priorReview,priorCurrentPowerReview:prior.receipt,reviewedChanges:review.reviewedChanges};
 return {review:prior.review,historical:prior.historical,receipt};
}

function verifyRetainedDeliveryReview(spec,kind,readSource){
 const binding=spec.retainedDeliveryCompatibilityReview;
 need(binding?.manifest==='tools/ymcc_sim/global/sleep-native-recovery-consumer/script-maintenance-retained-delivery.json'&&binding.sha256==='bcad13f0261ac1c4473be5e2a583a9432333e9c2a7097b9521be963a873546a8','RETAINED_DELIVERY_REVIEW_CONTRACT_CHANGED');
 const bytes=readSource(binding.manifest);need(digest(bytes)===binding.sha256,'RETAINED_DELIVERY_REVIEW_MANIFEST_CHANGED');
 const review=JSON.parse(bytes);need(review.schemaVersion===1&&review.kind==='retained-delivery-original-fixture-companions'&&review.sourceApproval===false&&review.defaultParentApprovalRenewed===false,'RETAINED_DELIVERY_REVIEW_SCHEMA');
 if(kind==='native')need(JSON.stringify(spec.originalScripts)===JSON.stringify(review.priorSpecs.native.originalScripts),'CANCELED_LEASE_SCRIPT_SET_CHANGED');
 if(kind==='pause'){const original=review.currentContracts.find(row=>row.path===review.priorSpecs.pause.originalScript.path);need(original&&spec.originalScript?.path===original.path&&spec.originalScript.sha256===original.sha256,'S4_REVIEW_SCRIPT_SET_CHANGED');}
 const historical=new Map();for(const row of review.deltas){need(!historical.has(row.path),'RETAINED_DELIVERY_DUPLICATE_DELTA');historical.set(row.path,reverseReviewedDelta(readSource(row.path),row));}
 const declared=kind==='pause'?[...spec.productSources,...spec.sharedLeaves]:spec.sources;
 for(const row of review.currentContracts){need(declared.includes(row.path),'RETAINED_DELIVERY_UNDECLARED_SOURCE');need(digest(readSource(row.path))===row.sha256,'RETAINED_DELIVERY_SOURCE_CHANGED:'+row.path);}
 if(kind==='pause'&&review.currentSharedLeafIdentities?.pause){need(JSON.stringify(spec.reviewedSharedLeafIdentities)===JSON.stringify(review.currentSharedLeafIdentities.pause),'RETAINED_DELIVERY_SHARED_LEAF_SET_CHANGED');for(const row of review.currentSharedLeafIdentities.pause){need(declared.includes(row.path),'RETAINED_DELIVERY_SHARED_LEAF_UNDECLARED');need(digest(readSource(row.path))===row.sha256,'RETAINED_DELIVERY_CURRENT_SHARED_LEAF_CHANGED:'+row.path);}const directPrior=new Map();for(const row of review.sharedLeafPriorPairing?.pause??[]){need(!directPrior.has(row.path),'RETAINED_DELIVERY_DUPLICATE_DIRECT_PRIOR');directPrior.set(row.path,reverseReviewedDelta(readSource(row.path),row));}for(const row of review.priorSpecs.pause.reviewedSharedLeafIdentities)need(digest(directPrior.get(row.path)??readSource(row.path))===row.sha256,'RETAINED_DELIVERY_PRIOR_SHARED_LEAF_CHANGED:'+row.path);}
 const paired=new Map();for(const row of review.priorPairing?.[kind]??[]){need(!paired.has(row.path),'RETAINED_DELIVERY_DUPLICATE_PRIOR_PAIR');paired.set(row.path,reverseReviewedDelta(readSource(row.path),row));}
 const prior=verifyS4Review(review.priorSpecs[kind],kind,relative=>paired.get(relative)??historical.get(relative)??readSource(relative));
 return {...prior,receipt:{...prior.receipt,retainedDeliveryReview:{manifest:binding.manifest,sha256:binding.sha256,exactReverseDeltas:true,currentSourceContractsVerified:true,sourceApproval:false,defaultParentApprovalRenewed:false,reviewedChanges:review.reviewedChanges}}};
}

const previousSourceSuccessorReviewHash='619fd23b1f324065e7fe8ab929c36006de76164d3242725f42c59c07956ce791';
const sourceSuccessorReviewHash='fa1d70309e12dab3ceb441c1ecce3678b82ac68310161ed479fe980dfb2d20d0';
function verifySourceSuccessorReview(spec,kind,readSource){
 const binding=spec.sourceSuccessorCompatibilityReview;
 need(binding?.manifest==='tools/ymcc_sim/global/sleep-native-recovery-consumer/script-maintenance-source-successor.json'&&binding.sha256===sourceSuccessorReviewHash,'SOURCE_SUCCESSOR_REVIEW_CONTRACT_CHANGED');
 const bytes=readSource(binding.manifest);need(digest(bytes)===sourceSuccessorReviewHash,'SOURCE_SUCCESSOR_REVIEW_MANIFEST_CHANGED');
 const review=JSON.parse(bytes);
 need(review.schemaVersion===1&&review.kind==='exact-source-successor-and-preserved-retained-history'&&review.sourceApproval===false&&review.defaultParentApprovalRenewed===false&&review.historicalWholeSourceApprovalRenewed===false,'SOURCE_SUCCESSOR_REVIEW_SCHEMA');
 need((kind==='native'||kind==='pause')&&review.priorSpecs?.[kind]&&review.currentSpecs?.[kind]&&!review.priorSpecs[kind].sourceSuccessorCompatibilityReview,'SOURCE_SUCCESSOR_PRIOR_SPEC');
 const currentSpec={...spec};delete currentSpec.sourceSuccessorCompatibilityReview;
 const declared=kind==='native'?spec.sources:[...spec.productSources,...spec.sharedLeaves];
 const {continuation,...previous}=review;
 need(continuation?.schemaVersion===1&&continuation.previousManifestSha256===previousSourceSuccessorReviewHash&&digest(JSON.stringify(previous,null,2)+'\n')===previousSourceSuccessorReviewHash,'SOURCE_SUCCESSOR_PREVIOUS_MANIFEST_CHANGED');
 need(JSON.stringify(currentSpec)===JSON.stringify(continuation.currentSpecs?.[kind]),'SOURCE_SUCCESSOR_SPEC_CHANGED');
 const reconstructed=new Map();
 for(const {users,delta} of continuation.deltas){
  if(!users.includes(kind))continue;
  need(declared.includes(delta.path)&&!reconstructed.has(delta.path),'SOURCE_SUCCESSOR_CONTINUATION_DELTA');
  reconstructed.set(delta.path,reverseReviewedDelta(readSource(delta.path),delta));
 }
 const priorDeclared=kind==='native'?previous.currentSpecs.native.sources:[...previous.currentSpecs.pause.productSources,...previous.currentSpecs.pause.sharedLeaves];
 const added=continuation.added.filter(row=>row.kind===kind);
 need(JSON.stringify(added.map(x=>x.path))===JSON.stringify([...new Set(declared)].filter(name=>!priorDeclared.includes(name))),'SOURCE_SUCCESSOR_CONTINUATION_DEPENDENCIES');
 for(const row of added)need(digest(readSource(row.path))===row.sha256,'SOURCE_SUCCESSOR_NEW_DEPENDENCY_CHANGED:'+row.path);
 const previousRead=relative=>reconstructed.get(relative)??readSource(relative);
 need(Array.isArray(review.deltas)&&Array.isArray(review.currentSourceContracts)&&review.deltas.length===review.currentSourceContracts.length&&review.deltas.length>0,'SOURCE_SUCCESSOR_DELTA_SET');
 const historical=new Map();
 for(let i=0;i<review.deltas.length;i++){
  const row=review.deltas[i],contract=review.currentSourceContracts[i];
  need(row.path===contract.path&&row.afterSha256===contract.sha256&&declared.includes(row.path)&&!historical.has(row.path),'SOURCE_SUCCESSOR_SOURCE_CONTRACT');
  historical.set(row.path,reverseReviewedDelta(previousRead(row.path),row));
 }
 const prior=verifyS4Review(review.priorSpecs[kind],kind,relative=>historical.get(relative)??previousRead(relative));
 return {...prior,receipt:{...prior.receipt,sourceSuccessorReview:{manifest:binding.manifest,sha256:binding.sha256,previousManifestSha256:previousSourceSuccessorReviewHash,continuedInputs:reconstructed.size,addedDependencies:added.map(x=>x.path),exactReverseDeltas:true,currentSourceContractsVerified:true,historicalIdentitiesRetained:true,sourceApproval:false,defaultParentApprovalRenewed:false,historicalWholeSourceApprovalRenewed:false,reviewedChanges:review.reviewedChanges}}};
}

export function verifyS4Review(spec,kind,readSource){
 if(spec.sourceSuccessorCompatibilityReview)return verifySourceSuccessorReview(spec,kind,readSource);
 if(spec.retainedDeliveryCompatibilityReview)return verifyRetainedDeliveryReview(spec,kind,readSource);
 if(kind==='native'&&spec.canceledLeaseCompatibilityReview)return verifyCanceledLeaseReview(spec,readSource);
 if(kind==='native'&&spec.currentPowerCompatibilityReview)return verifyCurrentPowerReview(spec,readSource);
 const binding=spec.s4CompatibilityReview;
 need(binding?.sha256===reviews[kind],'S4_REVIEW_CONTRACT_CHANGED');
 const bytes=readSource(binding.manifest);need(digest(bytes)===binding.sha256,'S4_REVIEW_MANIFEST_CHANGED');
 const review=JSON.parse(bytes),historical=new Map();
 need(review.schemaVersion===1&&review.kind===kind&&review.sourceApproval===false,'S4_REVIEW_SCHEMA');
 const feedbackApiPath='tools/sleep_recovery_api_selftest.py';let priorFeedbackApiBytes;
 if(kind==='native'){
  const delta=review.feedbackRotationApiDelta;
  need(delta?.path===feedbackApiPath&&delta.beforeSha256==='c2ea46f3db9d1d74bbb032bf9fe69becf529643feb7d22bbc4c9c08b20a4e709','S4_FEEDBACK_API_DELTA_CHANGED');
  const current=review.currentOriginalScripts.filter(row=>row.path===feedbackApiPath),declared=spec.originalScripts.filter(row=>row.path===feedbackApiPath),prior=review.deltas.filter(row=>row.path===feedbackApiPath);
  need(current.length===1&&declared.length===1&&prior.length===1&&current[0].sha256===delta.afterSha256&&declared[0].sha256===delta.afterSha256&&prior[0].afterSha256===delta.beforeSha256,'S4_FEEDBACK_API_BINDING_CHANGED');
  priorFeedbackApiBytes=reverseReviewedDelta(readSource(feedbackApiPath),delta);
 }
 for(const row of review.deltas){need(!historical.has(row.path),'S4_REVIEW_DUPLICATE_SOURCE');const bytes=kind==='native'&&row.path===feedbackApiPath?priorFeedbackApiBytes:readSource(row.path);historical.set(row.path,reverseReviewedDelta(bytes,row));}
 for(const row of review.contracts)need(digest(readSource(row.path))===row.sha256,'S4_REVIEW_SOURCE_CONTRACT_CHANGED:'+row.path);
 return {review,historical,receipt:{schemaVersion:1,status:'PASS',manifest:binding.manifest,sha256:binding.sha256,exactReverseDeltas:true,historicalIdentitiesRetained:true,sourceApproval:false}};
}
export function verifyCurrentReviewedScripts(spec,readSource){
 if(spec.sourceSuccessorCompatibilityReview)return verifySourceSuccessorReview(spec,'native',readSource).receipt;
 if(spec.retainedDeliveryCompatibilityReview)return verifyRetainedDeliveryReview(spec,'native',readSource).receipt;
 if(spec.canceledLeaseCompatibilityReview)return verifyCanceledLeaseReview(spec,readSource).receipt;
 if(spec.currentPowerCompatibilityReview)return verifyCurrentPowerReview(spec,readSource).receipt;
 const checked=verifyS4Review(spec,'native',readSource);
 need(JSON.stringify(spec.originalScripts)===JSON.stringify(checked.review.currentOriginalScripts),'S4_REVIEW_SCRIPT_SET_CHANGED');
 const prior=verifyReviewedOriginalScripts(checked.review.priorSpec,relative=>checked.historical.get(relative)??readSource(relative));
 return {...checked.receipt,priorReview:prior,reviewedChanges:checked.review.reviewedChanges};
}
export function verifyPauseSourceReview(repo,spec){
 return verifyS4Review(spec,'pause',relative=>fs.readFileSync(path.join(repo,relative))).receipt;
}
