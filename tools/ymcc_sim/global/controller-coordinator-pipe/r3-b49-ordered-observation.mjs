// Ordered trace contract. Counts of checks and children come only from a report.
// This module cannot promote static projection or JS samples to native execution.
import fs from 'node:fs';
import {hashSource} from '../../../input_coordinator_b49_source_structure.mjs';
const sealed=JSON.parse(fs.readFileSync(new URL('./r3-b49-source-order.json',import.meta.url),'utf8'));
const need=(value,code)=>{if(!value)throw Error('B49_ORDERED_'+code);};
export function b49OrderedPlan(){return structuredClone(sealed.occurrences);}
export function observeB49OrderedReport(report){
 const expected=b49OrderedPlan();
 need(sealed.kind==='STATIC_SOURCE_PROJECTION_NOT_NATIVE_EXECUTION','STATIC_PLAN_LABEL_REQUIRED');
 need(Array.isArray(report?.cases)&&report.cases.length===expected.length,'COMPLETE_SOURCE_OCCURRENCES_REQUIRED');
 need(Array.isArray(report.assertionTrace)&&Array.isArray(report.failures),'ORIGINAL_TRACES_REQUIRED');
 const cases=report.cases.map((actual,index)=>{const row=expected[index];need(actual.name===row.name,'SOURCE_ORDER_CHANGED:'+row.sourceOrdinal);need(actual.sourceOrdinal===row.sourceOrdinal&&actual.nameOccurrence===row.nameOccurrence,'CASE_OCCURRENCE_CHANGED:'+row.sourceOrdinal);return {...structuredClone(actual),occurrenceId:row.occurrenceId};});
 const assertions=new Map(),checkCounts=new Map();let previous=1;
 const assertionTrace=report.assertionTrace.map((actual,index)=>{
  need(actual.checkIndex===index+1,'RUNTIME_CHECK_SEQUENCE_CHANGED');
  const row=expected[actual.sourceOrdinal-1];
  need(row&&actual.case===row.name&&actual.nameOccurrence===row.nameOccurrence&&typeof actual.assertion==='string'&&actual.assertion.length>0&&typeof actual.passed==='boolean','ASSERTION_OCCURRENCE_REQUIRED');
  need(actual.sourceOrdinal>=previous,'CHECK_CASE_ORDER_CHANGED');previous=actual.sourceOrdinal;
  const key=JSON.stringify([row.sourceOrdinal,actual.assertion]),occurrence=(assertions.get(key)??0)+1;assertions.set(key,occurrence);checkCounts.set(row.sourceOrdinal,(checkCounts.get(row.sourceOrdinal)??0)+1);
  return {...structuredClone(actual),occurrenceId:row.occurrenceId,assertionOccurrence:occurrence};
 });
 need(report.checks===assertionTrace.length,'RUNTIME_CHECK_TOTAL_DIFFERS');
 need(expected.every(row=>checkCounts.has(row.sourceOrdinal)),'UNOBSERVED_CASE_CHECKS');
 const failed=report.assertionTrace.filter(row=>!row.passed);
 need(failed.length===report.failures.length&&report.failures.every((failure,index)=>{const actual=failed[index];return failure.case===actual.case&&failure.assertion===actual.assertion&&failure.sourceOrdinal===actual.sourceOrdinal&&failure.nameOccurrence===actual.nameOccurrence&&failure.checkIndex===actual.checkIndex;}),'ORIGINAL_FAILURES_CHANGED');
 need(report.status===(failed.length?'FAIL':'PASS'),'STATUS_CONTRADICTS_BUSINESS_ASSERTIONS');
 const boundary=report.orderedReturn?.legacy95Boundary,post=report.orderedReturn?.postTargetReturnTail;
 need(boundary?.afterSourceOrdinal===sealed.original95WitnessBoundary&&post?.afterSourceOrdinal===expected.length,'SEPARATE_DEBT_BOUNDARIES_REQUIRED');
 need(boundary.lastCaseName===expected[boundary.afterSourceOrdinal-1].name&&post.lastCaseName===expected.at(-1).name,'BOUNDARY_CASE_CHANGED');
 need(boundary.checks===assertionTrace.filter(row=>row.sourceOrdinal<=sealed.original95WitnessBoundary).length&&post.checks===report.checks,'BOUNDARY_CHECK_OFFSET_DIFFERS');
 need(typeof boundary.resources?.msiModeDebt==='boolean'&&typeof post.resources?.msiModeDebt==='boolean','OBSERVED_DEBT_VALUES_REQUIRED');
 need(Array.isArray(boundary.nativeEvents)&&Array.isArray(post.nativeEvents),'BOUNDARY_RAW_EVENTS_REQUIRED');
 return {status:'ORDERED_OBSERVATION_ONLY',businessAcceptancePerformed:false,originalStatus:report.status,cases,assertionTrace,failures:structuredClone(report.failures),orderedReturn:structuredClone(report.orderedReturn),observed:{cases:cases.length,uniqueNames:new Set(cases.map(row=>row.name)).size,checks:report.checks},expectedRuntimeChecks:null,expectedOwnedChildren:null,partitionClosureVerified:false};
}
export function verifyOriginal95BoundaryWitness(boundary){
 need(boundary?.afterSourceOrdinal===sealed.original95WitnessBoundary&&boundary.lastCaseName==='OFF-LIFECYCLE/MSI-return-unproven','LEGACY_WITNESS_POSITION_CHANGED');
 need(boundary.resources?.msiModeDebt===true,'LEGACY_MODE_DEBT_WITNESS_LOST');
 need(Number.isSafeInteger(boundary.resources.msiModeDebtEpisode)&&boundary.resources.msiModeDebtEpisode>0,'LEGACY_DEBT_EPISODE_MISSING');
 return {status:'ORIGINAL95_DEBT_WITNESS_ONLY',postTailPolarityRequired:null,partitionClosureVerified:false};
}
// Read the original producer's mutation-to-business-assertion contract as data.
export function originalB49MutationAssertions(producer){
 need(hashSource(producer)==='4bcb657946c1b03c91243b5491f5eebec7ad461f26eee7f24d081caab19534e6','ORIGINAL_MUTATION_PRODUCER_REQUIRED');
 const marker='const expectedMutationAssertion=(',start=producer.indexOf(marker),end=producer.indexOf(')[mutation];',start);need(start>=0&&end>start,'ORIGINAL_MUTATION_MAP_REQUIRED');const object=JSON.parse(producer.slice(start+marker.length,end));need(object&&typeof object==='object','MUTATION_MAP');return object;
}
export function verifyB49BusinessOutcome(report,{execution,mutation=null,producerSource}={}){
 // Callers still own executable/managed/build provenance and must bind the raw
 // files. An exception, failed compilation, timeout or launch is never a fault.
 need(execution?.stage==='native-runtime'&&execution.compiledSuccessfully===true&&execution.timedOut===false&&execution.signal===null&&execution.launchError===null,'SUCCESSFUL_BUILD_AND_COMPLETE_NATIVE_EXECUTION_REQUIRED');
 need(report?.forbiddenIo===0&&report.fullTranslationUnit===true&&report.normalProductMainExecuted===false&&report.realDeviceIo===false&&!report.exception,'ORIGINAL_OFFLINE_EXECUTION_FLAGS_REQUIRED');
 const observation=observeB49OrderedReport(report),mutations=originalB49MutationAssertions(producerSource);verifyOriginal95BoundaryWitness(report.orderedReturn.legacy95Boundary);
 if(mutation){need(Object.hasOwn(mutations,mutation),'UNKNOWN_MUTATION');need(execution.exitCode===1&&report.status==='FAIL'&&report.failures.length>0,'REAL_BUSINESS_FAILURE_REQUIRED');need(report.failures.some(row=>row.assertion===mutations[mutation]),'EXPECTED_ORIGINAL_BUSINESS_ASSERTION_NOT_FAILED');}
 else need(execution.exitCode===0&&report.status==='PASS'&&report.failures.length===0,'POSITIVE_BUSINESS_ASSERTIONS_FAILED');
 return {status:'ORDERED_BUSINESS_CONTRACT_MATCHED',nativeExecutionProvenanceVerified:false,originalBusinessFailures:structuredClone(report.failures),observed:observation.observed,expectedRuntimeChecks:null,expectedOwnedChildren:null,partitionClosureVerified:false,publicSingle60Accepted:false};
}
