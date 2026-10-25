import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {R3_SPEC,verifyR3Shape} from './r3-contract.mjs';
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8'));
const need=(value,code)=>{if(!value)throw Error('B39_CANCELLATION_'+code);};
export const B39_ORACLE=read(new URL('./r3-cancellation-oracle.json',import.meta.url));
function closeBlock(text,start){let depth=0,state='code',quote='';for(let i=start;i<text.length;i++){const c=text[i],next=text[i+1];if(state==='line'){if(c==='\n')state='code';continue;}if(state==='block'){if(c==='*'&&next==='/'){state='code';i++;}continue;}if(state==='string'){if(c==='\\'){i++;continue;}if(c===quote)state='code';continue;}if(c==='/'&&next==='/'){state='line';i++;continue;}if(c==='/'&&next==='*'){state='block';i++;continue;}if(c==='"'||c==="'"){state='string';quote=c;continue;}if(c==='{')depth++;else if(c==='}'&&--depth===0)return i+1;}throw Error('B39_CANCELLATION_SOURCE_BLOCK_INCOMPLETE');}
function actualFunction(text,symbol){const re=new RegExp('^static [^\\r\\n;{}]*\\b'+symbol+'\\([^;{}]*\\)\\s*\\{','gm'),matches=[...text.matchAll(re)];need(matches.length===1,'FUNCTION_NOT_UNIQUE:'+symbol);const start=matches[0].index;return text.slice(start,closeBlock(text,text.indexOf('{',start)));}
export function loadCancellationContext(native){
 const sourceManifest=read(native.generated.find(r=>path.basename(r.path)==='source-manifest.json').path);
 const compiledProof=read(native.generated.find(r=>path.basename(r.path)==='RESULT.json').path);
 const inert=native.generated.find(r=>r.path.replaceAll('\\','/').endsWith('/source/native/main.inert.cpp'));
 const recipe=native.sources.find(r=>r.path==='tools/input_coordinator_lifecycle_selftest.mjs');
 need(inert&&recipe,'ACTUAL_MUTATED_INPUTS_REQUIRED');
 return {native,sourceManifest,compiledProof,inertText:fs.readFileSync(inert.path,'utf8'),recipeText:fs.readFileSync(recipe.snapshot,'utf8')};
}
export function verifyB39Cancellation(context,result,process){
 const o=B39_ORACLE,{native:n,sourceManifest:sm,compiledProof:proof,inertText,recipeText}=context;
 need(n.compileSucceeded===true&&n.status==='BUILT'&&n.expectedNegative===true&&n.mutation===o.mutation,'COMPILED_ORIGINAL_FAULT_REQUIRED');
 need(proof.mutation===o.mutation&&proof.changed?.length===0&&proof.results?.length===1&&proof.results[0].mode==='full-pipe'&&proof.results[0].exitCode===1&&proof.results[0].result?.status==='FAIL','ORIGINAL_COMPILED_NATIVE1_PROOF_REQUIRED');
 need(process?.exitCode===1&&!process.timedOut&&!process.launchError&&!process.signal&&!process.outputTruncated,'ORIGINAL_NATIVE1_REQUIRED');
 need(n.sources.find(r=>r.path==='native/main.cpp')?.sha256===o.mainSha256&&sm.records.find(r=>r.path==='native/main.cpp')?.sha256===o.mainSha256,'FINITE_SOURCE_IDENTITY');
 need(sha(recipeText)===o.recipeSourceSha256&&sha(inertText)===o.inertSha256,'ACTUAL_RECIPE_OR_MUTATION_IDENTITY');
 const recipeStart=recipeText.indexOf("if(mutation==='rollback-cancellation-guards'){");need(recipeStart>=0,'ORIGINAL_RECIPE_BLOCK_REQUIRED');const recipe=recipeText.slice(recipeStart,closeBlock(recipeText,recipeText.indexOf('{',recipeStart))).replaceAll('\r\n','\n');need(sha(recipe)===o.recipeLfSha256,'ORIGINAL_RECIPE_BYTES_REQUIRED');
 const fault=sm.substitutions.filter(r=>r.negativeControl);need(fault.length===1&&fault[0].symbol===o.core.map(r=>r.symbol).join('/'),'ORIGINAL_FOUR_FUNCTION_MUTATION_REQUIRED');
 for(const expected of o.core){const executed=actualFunction(inertText,expected.symbol);for(const ledger of [n.core,sm.core]){const row=ledger.find(r=>r.symbol===expected.symbol);need(row?.sha256===expected.sha256&&row.executedSha256===expected.executedSha256&&row.expectedSemanticNegative===true,'FOUR_FUNCTION_IDENTITY:'+expected.symbol);}need(sha(executed)===expected.executedSha256,'ACTUAL_EXECUTED_FUNCTION:'+expected.symbol);}
 const worker=actualFunction(inertText,'inputHostPerformPsWakeRearm');
 for(const token of ['admission-changed-before-rebuild','admission-changed-during-rebuild','view.requestId!=queryRequestId','g_inputHostEpoch!=queryHostEpoch','currentPowerGeneration()!=generation','g_psEndpointOwnership.revision()!=ownership.revision','inputHostExitFenceRaised()'])need(worker.includes(token),'RETAINED_PRODUCTION_GUARD:'+token);
 const reviewUrl=new URL('./'+o.independentReview.file,import.meta.url);need(typeof o.independentReview.sha256==='string'&&sha(fs.readFileSync(reviewUrl))===o.independentReview.sha256,'INDEPENDENT_REVIEW_REQUIRED');
 const review=read(reviewUrl),keysOf=rows=>rows.map(({case:c,assertion})=>JSON.stringify([c,assertion])).sort();need(review.actualMutation.originalAndCut47RecipeLFByteIdentical===true&&review.actualMutation.recipeSha256===o.recipeLfSha256&&JSON.stringify(keysOf(review.recommendedContractMaintenance.requiredFailInstances))===JSON.stringify(keysOf(o.expectedFailedInstances))&&JSON.stringify(keysOf(review.recommendedContractMaintenance.requiredPassInstances))===JSON.stringify(keysOf(o.originalInstancesNowRequiredPass)),'REVIEWED_EXACT_FAILURE_AND_PASS_RULES');
 const sourceReviewUrl=new URL('./'+o.sourceRebindReview.file,import.meta.url);need(sha(fs.readFileSync(sourceReviewUrl))===o.sourceRebindReview.sha256,'B39_SOURCE_REBIND_REVIEW_REQUIRED');const rebound=read(sourceReviewUrl);need(rebound.mainSha256===o.mainSha256&&JSON.stringify(rebound.actualCore)===JSON.stringify(o.core)&&rebound.sameFailureIdentityMultisetAsReviewedCut47===true&&rebound.allFourIndependentlyReviewedGuardFragmentsByteIdentical===true,'B39_SOURCE_AND_RETAINED_GUARD_REVIEW');
 const independentUrl=new URL('./'+o.independentSourceExtension.file,import.meta.url);need(sha(fs.readFileSync(independentUrl))===o.independentSourceExtension.sha256,'B39_INDEPENDENT_SOURCE_EXTENSION_REQUIRED');const independent=read(independentUrl);need(independent.sourceInputs.some(r=>r.main.sha256===o.mainSha256)&&independent.remainingCoreTailByteIdentical===true&&independent.coreComparison.length===4&&independent.coreComparison.every(r=>o.core.some(c=>c.symbol===r.symbol&&c.sha256===r.originalSha256&&c.executedSha256===r.executedSha256)),'B39_INDEPENDENT_ACTUAL_CORE_AND_SOURCE');for(const token of ['queuedCycle!=g_psWakeRequestCycleId.load','queuedCycle!=g_virtualStartProtection.view','unbound-queue-token','queued-request-context-retired'])need(worker.includes(token),'B39_QUEUE_CYCLE_GUARD:'+token);
 verifyR3Shape(result,{positive:false});
 need(result.status==='FAIL'&&result.checks===o.checks&&result.cases.length===o.groups,'COMPLETE_ORIGINAL_EXECUTION_REQUIRED');
 need(JSON.stringify(result.assertionTrace.map(({case:c,assertion})=>({case:c,assertion})))===JSON.stringify(R3_SPEC.positiveAssertionSequence),'ALL_CURRENT_ORIGINAL_ASSERTIONS_REQUIRED');
 const keys=rows=>rows.map(({case:c,assertion})=>JSON.stringify([c,assertion])).sort();
 need(JSON.stringify(keys(result.failures))===JSON.stringify(keys(o.expectedFailedInstances)),'EXACT18_FAILURE_INSTANCES_REQUIRED');
 const failedIndices=new Set(o.expectedFailedInstances.map(r=>r.checkIndex));
 need(result.assertionTrace.every(r=>r.passed===!failedIndices.has(r.checkIndex)),'EXACT_EXECUTED_FAILURE_AND_PASS_STATES_REQUIRED');
 for(const expected of [...o.expectedFailedInstances,...o.originalInstancesNowRequiredPass]){const row=result.assertionTrace[expected.checkIndex-1];need(row?.case===expected.case&&row.assertion===expected.assertion&&row.passed===!failedIndices.has(expected.checkIndex),'ORIGINAL29_INSTANCE_NOT_EXECUTED_AS_REQUIRED');}
 need(o.original35FailureInstances.length===29&&o.expectedFailedInstances.length===18&&o.originalInstancesNowRequiredPass.length===11&&JSON.stringify(keys(o.original35FailureInstances))===JSON.stringify(keys([...o.expectedFailedInstances,...o.originalInstancesNowRequiredPass])),'HISTORICAL29_ACCOUNTING_REQUIRED');
 for(const kind of ['off','exit','power','generation','persona','reentry']){
  const rows=result.cases.filter(r=>r.psWakeCancellation?.kind===kind);need(rows.length===1,'EXACT_CALLBACK_CASE_REQUIRED:'+kind);const row=rows[0],q=row.psWakeCancellation;
  need(q.callbackExecuted===true&&q.probeCalls===(kind==='generation'?2:1)&&q.markedRebuilt===false&&q.rearmRequested===false,'ACTUAL_CALLBACK_PROBE_AND_RETAINED_GUARD:'+kind);
  need(row.nativeEvents.some(e=>e.event==='ps-wake-rearm-deferred'&&e.detail.reason==='admission-changed-before-rebuild'&&e.detail.queryRequestId===q.requestIdBefore&&e.detail.currentRequestId===q.requestIdAfter),'LOCKED_GUARD_OBSERVATION:'+kind);
  const blocked=['generation','reentry'].includes(kind);
  need(q.recreateCommandsAdded===(blocked?0:1)&&q.directRecreateReturned===!blocked,'ACTUAL_RECREATE_FAULT_AND_RESIDUAL_GUARD:'+kind);
  need((kind==='persona'?row.psWakeNewIntentFollowup?.oldManagedReceipt?.creates:row.managedReceipt?.creates)===(blocked?1:2),'ACTUAL_MANAGED_TARGET_COUNT:'+kind);
  if(!blocked)need(result.failures.some(f=>f.case===row.name&&f.assertion==='actual RECREATE primitive refuses changed admission at send boundary'),'ORIGINAL_DESIGNATED_RECREATE_FAULT_REQUIRED:'+kind);
  if(['off','persona'].includes(kind))need(q.normalFrameResult?.hostSubmission==='frame-accepted'&&q.unboundFrameAfterOff?.hostSubmission==='frame-accepted','ORIGINAL_INPUT_FAULT_REQUIRED:'+kind);
  if(kind==='generation')need(q.newerRequestPreserved===true&&q.newerRequestCompleted===true,'NEWER_GENERATION_GUARD_REQUIRED');
  if(kind==='reentry')need(q.requestIdBefore!==q.requestIdAfter,'NEW_REENTRY_REQUEST_REQUIRED');
 }
 return {id:o.id,cutpoint:o.cutpoint,mutation:o.mutation,mainSha256:o.mainSha256,originalNativeExitCode:1,groups:o.groups,checks:o.checks,exactFailedInstances:18,originalInstancesExecutedAndPassed:11,allOriginal29Accounted:true,independentReview:o.independentReview,original35OracleRewritten:false};
}
