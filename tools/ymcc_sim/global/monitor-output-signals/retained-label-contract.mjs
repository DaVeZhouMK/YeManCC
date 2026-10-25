import {assertionCalls} from './common.mjs';
import {OLD_LABEL_ASSERT,NEW_LABEL_ASSERT} from './preset-contract.mjs';
const need=(ok,code)=>{if(!ok)throw Error('TEMPLATE_LABEL_'+code);};
export function classifyTemplateLabelContract(text){
 const calls=assertionCalls(text),oldCount=calls.filter(call=>call===OLD_LABEL_ASSERT).length,currentCount=calls.filter(call=>call===NEW_LABEL_ASSERT).length;
 need(oldCount+currentCount===1&&((oldCount===1&&currentCount===0)||(oldCount===0&&currentCount===1)),'EXPECTED_ONE_REVIEWED_LABEL_ASSERTION');
 return {schemaVersion:1,mode:oldCount?'legacy-label-expectation':'already-current-label-expectation',oldCount,currentCount,originalAssertionCalls:calls.length};
}
export function verifyTemplateLabelOutcome(before,after){
 const a=before?.sourceLabelContract,b=after?.sourceLabelContract;
 need(a&&b&&a.schemaVersion===1&&JSON.stringify(a)===JSON.stringify(b),'SOURCE_CONTRACT_IDENTITY');
 need(before.id==='templates'&&after.id==='templates'&&/^[a-f0-9]{64}$/.test(before.sourceSha256??'')&&before.sourceSha256===after.sourceSha256,'ORIGINAL_PRODUCER_IDENTITY');
 need(Number.isInteger(a.originalAssertionCalls)&&a.originalAssertionCalls>0,'ORIGINAL_ASSERTION_SCHEMA');
 need(Array.isArray(before.metadataAssertionChanges)&&Array.isArray(after.metadataAssertionChanges),'METADATA_CHANGE_SCHEMA');
 need(before.originalAssertionCalls===a.originalAssertionCalls&&after.originalAssertionCalls===a.originalAssertionCalls&&before.afterAssertionCalls===a.originalAssertionCalls&&after.afterAssertionCalls===a.originalAssertionCalls,'ORIGINAL_ASSERTION_COUNT');
 need(before.allBehaviorAssertionsPreserved===true&&after.allBehaviorAssertionsPreserved===true,'ORIGINAL_BEHAVIOR_NOT_PRESERVED');
 const original=before.receipt?.fullCases,candidate=after.receipt?.fullCases;
 need(Array.isArray(original)&&original.length>0&&Array.isArray(candidate)&&JSON.stringify(original.map(row=>row.label))===JSON.stringify(candidate.map(row=>row.label)),'ORIGINAL_CASE_ORDER');
 need(after.status==='PASS'&&after.exitCode===0&&candidate.every(row=>row.status==='PASS'),'RETAINED_ORIGINAL_CASE_FAILED');
 const failures=original.filter(row=>row.status!=='PASS');
 if(a.mode==='already-current-label-expectation'){
  need(a.oldCount===0&&a.currentCount===1&&before.status==='PASS'&&before.exitCode===0&&failures.length===0,'CURRENT_ORIGINAL_NOT_PASS');
  need(before.allOriginalAssertionsPreserved===true&&after.allOriginalAssertionsPreserved===true&&before.metadataAssertionChanges.length===0&&after.metadataAssertionChanges.length===0,'CURRENT_SOURCE_MUST_NOT_BE_REWRITTEN');
 }else{
  need(a.mode==='legacy-label-expectation'&&a.oldCount===1&&a.currentCount===0,'UNKNOWN_SOURCE_STATE');
  need(before.status==='FAIL'&&before.exitCode===1&&failures.length===1&&failures[0].label==='Default simple preset uses capsule/Tasha and the screenshot selection','LEGACY_FAILURE_NOT_EXACT');
  need(before.allOriginalAssertionsPreserved===true&&before.metadataAssertionChanges.length===0&&after.metadataAssertionChanges.length===1,'LEGACY_CHANGE_COUNT');
  const change=after.metadataAssertionChanges[0];need(change.before===OLD_LABEL_ASSERT&&change.after===NEW_LABEL_ASSERT&&change.scope==='label metadata only','UNREVIEWED_METADATA_CHANGE');
 }
 return {schemaVersion:1,status:'PASS',mode:a.mode,labelChangeApplied:a.mode==='legacy-label-expectation',originalCases:original.length,allBehaviorAssertionsPreserved:true,currentProducerNotRewritten:a.mode==='already-current-label-expectation'};
}

