// The explicit26 ordinary switch consumer. Legacy43/921 remains in r3-contract.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
const need=(ok,code)=>{if(!ok)throw Error('R3_TERMINAL26_SWITCH_'+code);};
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
let expectations;
function spec(){return expectations??=JSON.parse(fs.readFileSync(new URL('./r3-terminal26-switch-expectations.json',import.meta.url),'utf8'));}
export function terminal26SwitchFunction(bytes){
 need(Buffer.isBuffer(bytes),'SOURCE_BYTES_REQUIRED');const text=bytes.toString('utf8');
 const masked=text.replace(/\/\*[\s\S]*?\*\/|\/\/[^\r\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,v=>v.replace(/[^\r\n]/g,' '));
 const marker='static void switchRecoveryCases(',start=masked.indexOf(marker);
 need(start>=0&&masked.indexOf(marker,start+marker.length)<0,'EXACT_SOURCE_FUNCTION_REQUIRED');
 const open=masked.indexOf('{',start);need(open>start,'SOURCE_BODY_REQUIRED');let depth=1,end=open+1;
 for(;end<masked.length&&depth;end++){if(masked[end]==='{')depth++;else if(masked[end]==='}')depth--;}
 need(depth===0,'SOURCE_BODY_INCOMPLETE');return Buffer.from(text.slice(start,end));
}
function originalHost(c){
 const b=c.transitionEvidence;
 need(b&&b.sameOldHandle===true&&b.changed===false&&b.createCount===1&&b.stopCount===1,'ORIGINAL_HOST_WITNESS_REQUIRED');
 need(Number.isSafeInteger(b.oldEpoch)&&b.oldEpoch>0&&b.observedEpoch===b.oldEpoch&&typeof b.oldRunId==='string'&&b.oldRunId.length>0&&
  b.observed?.hostHandle===true&&b.observed.runId===b.oldRunId&&b.observed.epoch===b.oldEpoch,'ORIGINAL_OWNER_CONFLICT');
 return true;
}
function returnedHost(c){
 const b=c.transitionEvidence,a=c.afterOldExit,f=c.finalExecution;
 need(a&&f&&a.createCount===2&&a.state?.prepared===true&&a.state.quarantined===false&&a.state.hostHandle===true&&
  a.state.epoch>b.oldEpoch&&a.state.runId!==b.oldRunId&&a.requestId===b.requestId,'RETURNED_HOST_WITNESS_REQUIRED');
 need(f.requestId===b.requestId&&f.createCount===2&&f.stopCount===1&&f.phase==='success'&&f.locked===false&&f.fallbackAttempts===0&&
  Number.isSafeInteger(f.elapsedMs)&&f.elapsedMs>=10000&&f.elapsedMs<30000,'RETURNED_FINAL_CONFLICT');return true;
}
function rootStage(c){
 const s=c.normalConfigurationSnapshots;
 need(Array.isArray(s)&&s.length>=1,'ROOT_SNAPSHOT_REQUIRED');const r=s[0],debt=r.debtAfter;
 need(r.valid===true&&r.known===true&&Array.isArray(r.hidden)&&r.hidden.length===2&&new Set(r.hidden).size===2&&
  debt?.configurationApplied===true&&debt.configurationTargetCount===2&&Array.isArray(debt.configurationTargets)&&
  debt.configurationTargets.length===2&&r.hidden.every(v=>debt.configurationTargets.includes(v))&&
  typeof debt.configurationRoot==='string'&&debt.configurationRoot.length>0,'ROOT_SNAPSHOT_CONFLICT');return true;
}
function readbackTargets(c){
 // This case directly captures/publishes stamps; it never calls the helper
 // which emits `readbacks`. The exact capture assertions remain in its grammar.
 // Bind the existing owned hide and Host transaction instead of inventing output.
 need(Array.isArray(c.events)&&Array.isArray(c.trace),'OWNED_QUERY_EVENTS_REQUIRED');
 const hidden=c.events.filter(e=>e.event==='hidhide.pre-cloak').map(e=>e.detail);
 const hosts=c.events.filter(e=>e.event==='input-host-started').map(e=>e.detail);
 need(hidden.length===1&&hosts.length===1&&hidden[0].armed===true&&Number.isSafeInteger(hidden[0].blocked)&&hidden[0].blocked>0&&
  hosts[0].transportUsable===true&&Number.isSafeInteger(hosts[0].epoch)&&hosts[0].epoch>0&&typeof hosts[0].runId==='string'&&
  hosts[0].runId.length>0&&hidden[0].transactionId===hosts[0].runId,'NONEMPTY_QUERY_TARGETS_REQUIRED');
 const apply=c.trace.indexOf('hidhide-apply'),query=c.trace.indexOf('pipe:HIDHIDE_QUERY'),prepare=c.trace.indexOf('pipe:PREPARE_TARGET');
 need(apply>=0&&apply<query&&query<prepare&&c.trace.filter(v=>v==='create-process').length===1,'OWNED_QUERY_ORDER_CONFLICT');return true;
}
function cleanupAwaiting(c){
 need(Array.isArray(c.events)&&Array.isArray(c.trace),'CLEANUP_EVENT_TRACE_REQUIRED');
 const rows=c.events.filter(e=>e.event==='input-host-switch-recovery').map(e=>e.detail);
 const begins=rows.filter(r=>r.stage==='started'&&r.reason==='classified-stop-reopen'),ends=rows.filter(r=>r.stage==='result');
 need(begins.length===1&&ends.length===1,'CLEANUP_SINGLE_ATTEMPT_REQUIRED');const begin=begins[0],end=ends[0];
 need(begin.requestId>0&&rows.every(r=>r.requestId===begin.requestId&&r.deadline===begin.deadline&&r.powerGeneration===begin.powerGeneration),'CLEANUP_OWNER_CONFLICT');
 need(begin.spent===true&&begin.awaitingCleanup===false&&begin.fallbackAttempts===1&&end.spent===true&&end.stopInvoked===true&&
  end.awaitingCleanup===false&&end.pending===false&&end.startReturned===true&&end.fallbackAttempts===1,'CLEANUP_RESULT_CONFLICT');
 need(c.trace.filter(v=>v==='stop:switch-recovery').length===1&&c.trace.filter(v=>v==='create-process').length===2,'CLEANUP_STOP_START_CONFLICT');
 const held=rows.filter(r=>r.awaitingCleanup===true),resumes=rows.filter(r=>r.stage==='started'&&r.reason==='resume-start-after-cleanup');
 if(held.length===0){need(resumes.length===0,'CLEANUP_FOREIGN_RESUME');return false;}
 need(held.length===1&&held[0].stage==='held'&&held[0].stopInvoked===true&&held[0].spent===true&&
  ['awaiting-existing-return-proof','awaiting-post-cleanup-pause'].includes(held[0].reason)&&resumes.length===1&&
  rows.indexOf(begin)<rows.indexOf(held[0])&&rows.indexOf(held[0])<rows.indexOf(resumes[0])&&rows.indexOf(resumes[0])<rows.indexOf(end),'CLEANUP_HELD_WITNESS_CONFLICT');return true;
}
function offCleanup(c){
 need(Array.isArray(c.events),'OFF_EVENTS_REQUIRED');
 const finals=c.events.filter(e=>e.event==='input-host-switch-verification'&&e.detail?.stage==='final');
 need(finals.length===1&&finals[0].detail.outcome==='success','OFF_SINGLE_SUCCESS_REQUIRED');const evidence=finals[0].detail.cleanupEvidence;
 need(evidence&&same(evidence.blockers,[])&&same(evidence.unknown,[])&&evidence.checks&&Object.keys(evidence.checks).length>0&&
  Object.values(evidence.checks).every(v=>v===true)&&evidence.checks.msiSteamDeckDebtCleared===true,'OFF_CLEANUP_DEBT_UNSETTLED');
}
export function verifyTerminal26Switch(result,{switchSource}={}){
 const contract=spec();
 need(contract.kind==='EXPLICIT26_SOURCE_DERIVED_SWITCH_ASSERTION_GRAMMAR_V1'&&contract.cases.length===98,'SOURCE_CONTRACT_REQUIRED');
 need(switchSource&&typeof switchSource.path==='string'&&path.isAbsolute(switchSource.path)&&path.basename(switchSource.path)==='fixture.cpp'&&
  Buffer.isBuffer(switchSource.bytes)&&sha(switchSource.bytes)===switchSource.sha256,'COMPILED_SOURCE_PIN_REQUIRED');
 const body=terminal26SwitchFunction(switchSource.bytes);
 need(body.length===contract.sourceBindings.switchFunctionBytes&&sha(body)===contract.sourceBindings.exactSwitchSourceSha256,'COMPILED_SOURCE_FUNCTION_CONFLICT');
 need(result?.status==='PASS'&&Array.isArray(result.failures)&&result.failures.length===0&&result.forbiddenIo===0&&
  result.fullTranslationUnit===true&&result.realDeviceIo===false&&result.normalProductMainExecuted===false,'POSITIVE_NATIVE_RESULT_REQUIRED');
 need(Array.isArray(result.cases)&&result.cases.length===98&&Array.isArray(result.assertionTrace)&&Number.isSafeInteger(result.checks),'EXACT98_REQUIRED');
 const expected=[],witnesses=[];
 function expand(nodes,facts,row){for(const node of nodes){
  if(Number.isInteger(node)){need(typeof contract.labels[node]==='string','GRAMMAR_LABEL_REQUIRED');expected.push({case:row.name,sourceOrdinal:row.sourceOrdinal,nameOccurrence:1,assertion:contract.labels[node]});}
  else{need(node?.fact==='cleanupAwaiting'&&typeof facts[node.fact]==='boolean','INDEPENDENT_BRANCH_FACT_REQUIRED');expand(facts[node.fact]?node.then:node.else,facts,row);}
 }}
 result.cases.forEach((c,i)=>{
  const row=contract.cases[i];need(c?.name===row.name&&c.sourceOrdinal===i+1&&row.sourceOrdinal===i+1&&c.nameOccurrence===1,'EXACT_CASE_ORDER_REQUIRED');
  const facts={};for(const kind of row.requiredWitnesses){
   if(kind==='exactOriginalHost')facts[kind]=originalHost(c);
   else if(kind==='returnedNewHost')facts[kind]=returnedHost(c);
   else if(kind==='initialRootStage')facts[kind]=rootStage(c);
   else if(kind==='nonemptyReadbackTargets')facts[kind]=readbackTargets(c);
   else if(kind==='cleanupAwaiting')facts[kind]=cleanupAwaiting(c);
   else need(false,'UNKNOWN_WITNESS_KIND');
  }
  if(i===97)offCleanup(c);witnesses.push({sourceOrdinal:i+1,...facts});expand(row.grammar,facts,row);
 });
 need(result.checks===expected.length&&result.assertionTrace.length===expected.length,'EXACT_ASSERTION_COUNT_REQUIRED');
 expected.forEach((row,i)=>{const actual=result.assertionTrace[i];need(actual&&actual.checkIndex===i+1&&actual.passed===true&&Object.keys(row).every(k=>actual[k]===row[k]),'EXACT_ASSERTION_SEQUENCE_REQUIRED');});
 return {kind:contract.kind,cases:98,checks:expected.length,witnesses,compiledSwitchSha256:sha(body),runtimeAuthorityGranted:false};
}
