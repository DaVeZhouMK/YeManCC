/** Explicit producer terminal contracts. No producer, process or command runs. */
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
const sha=value=>crypto.createHash('sha256').update(value).digest('hex');
const need=(value,code)=>{if(!value)throw Error('COMPLETION_CONTRACT_INVALID:'+code);};
const text=value=>typeof value==='string'&&value.length>0&&value.length<=512&&!/[\0\r\n]/.test(value);
export function normalizeCompletionContract(contract){
 need(contract&&typeof contract==='object'&&!Array.isArray(contract),'OBJECT_REQUIRED');
 need(Object.keys(contract).every(key=>['schemaVersion','rules'].includes(key))&&contract.schemaVersion===1,'SCHEMA');
 need(Array.isArray(contract.rules)&&contract.rules.length>0&&contract.rules.length<=16,'RULES');
 const rules=contract.rules.map(rule=>{
  need(rule&&typeof rule==='object'&&!Array.isArray(rule),'RULE_OBJECT');const stream=Object.hasOwn(rule,'stream')?rule.stream:'stdout';need(['stdout','stderr'].includes(stream),'STREAM');
  if('line'in rule){need(Object.keys(rule).every(key=>['stream','line'].includes(key))&&text(rule.line),'LITERAL');return{stream,line:rule.line};}
  need(Object.keys(rule).every(key=>['stream','prefix','suffix','minimum'].includes(key))&&text(rule.prefix)&&text(rule.suffix)&&Number.isSafeInteger(rule.minimum)&&rule.minimum>=1&&rule.minimum<=1_000_000_000,'NUMERIC_SUMMARY');
  return{stream,prefix:rule.prefix,suffix:rule.suffix,minimum:rule.minimum};
 });
 need(new Set(rules.map(rule=>JSON.stringify(rule))).size===rules.length,'DUPLICATE_RULE');return{schemaVersion:1,rules};
}
export const completionContractFingerprint=contract=>sha(JSON.stringify(normalizeCompletionContract(contract)));
export function verifyExecutionCompletion(processResult,contract){
 const normalized=normalizeCompletionContract(contract),streams={stdout:typeof processResult?.stdout==='string'?processResult.stdout:'',stderr:typeof processResult?.stderr==='string'?processResult.stderr:''};
 const bounded=Object.values(streams).every(value=>Buffer.byteLength(value)<=2*1024*1024);
 const natural=processResult?.exitCode===0&&!processResult.signal&&!processResult.timedOut&&!processResult.launchError&&!processResult.outputTruncated&&!processResult.sourceChanged&&!processResult.candidateArtifactChanged&&!processResult.missingInputs&&!processResult.buildError&&!processResult.requiredEvidenceMissing&&!processResult.requiredEvidenceFailed;
 const checks=normalized.rules.map(rule=>{
  const lines=bounded?streams[rule.stream].split(/\r?\n/):[];
  const matching=lines.filter(line=>'line'in rule?line===rule.line:line.startsWith(rule.prefix)&&line.endsWith(rule.suffix));
  let observed=null,numberValid=true;
  if(!('line'in rule)&&matching.length===1){const number=matching[0].slice(rule.prefix.length,-rule.suffix.length);numberValid=/^(0|[1-9][0-9]*)$/.test(number)&&number.length<=10;observed=numberValid?Number(number):null;numberValid&&=Number.isSafeInteger(observed)&&observed>=rule.minimum;}
  const passed=matching.length===1&&numberValid;
  return{rule,matches:matching.length,observed,passed,code:matching.length===0?'TERMINAL_LINE_MISSING':matching.length>1?'TERMINAL_LINE_DUPLICATED':!numberValid?'SUMMARY_BELOW_REQUIRED_OR_MALFORMED':'TERMINAL_LINE_OBSERVED'};
 });
 return{schemaVersion:1,kind:'ymcc-execution-completion',status:natural&&bounded&&checks.every(row=>row.passed)?'PASS':'FAIL',contract:normalized,contractFingerprint:completionContractFingerprint(normalized),streamHashes:{stdout:sha(streams.stdout),stderr:sha(streams.stderr)},checks,
  processAccepted:natural,outputWithinBudget:bounded,executedTests:0,meaning:'Only the declared terminal output of an already recorded process. Does not execute or complete the producer, prove branch coverage, or establish physical behavior.'};
}
export function completionProofMatchesContract(proof,contract){try{return proof?.schemaVersion===1&&proof.kind==='ymcc-execution-completion'&&proof.status==='PASS'&&proof.contractFingerprint===completionContractFingerprint(contract)&&proof.contractFingerprint===completionContractFingerprint(proof.contract);}catch{return false;}}
function ownStream(caseDir,name){
 const root=fs.realpathSync(caseDir),file=fs.realpathSync(path.join(caseDir,name));const relative=path.relative(root,file);
 if(relative==='..'||relative.startsWith('..'+path.sep)||path.isAbsolute(relative)||!fs.statSync(file).isFile()||fs.statSync(file).size>2*1024*1024)throw Error('COMPLETION_STREAM_OUTSIDE_CASE_OR_TOO_LARGE');
 return fs.readFileSync(file,'utf8');
}
export function verifySavedExecutionCompletion(processResult,caseDir,contract=processResult?.completionProof?.contract){
 try{
  if(!completionProofMatchesContract(processResult?.completionProof,contract))return{status:'FAIL',code:'COMPLETION_PROOF_MISSING_OR_CONTRACT_CHANGED'};
  const actual=verifyExecutionCompletion({...processResult,stdout:ownStream(caseDir,'stdout.log'),stderr:ownStream(caseDir,'stderr.log')},contract);
  const unchanged=actual.streamHashes.stdout===processResult.completionProof.streamHashes?.stdout&&actual.streamHashes.stderr===processResult.completionProof.streamHashes?.stderr;
  return{status:actual.status==='PASS'&&unchanged?'PASS':'FAIL',code:!unchanged?'COMPLETION_STREAM_CHANGED':actual.status==='PASS'?'COMPLETION_RAW_STREAMS_VERIFIED':'COMPLETION_RAW_STREAMS_NOT_COMPLETE',contractFingerprint:actual.contractFingerprint};
 }catch{return{status:'FAIL',code:'COMPLETION_RAW_STREAMS_MISSING_OR_INVALID'};}
}
export function knownSettingsCompletionContract(id){
 if(id==='settings.reactivity')return{schemaVersion:1,rules:[{prefix:'settings_reactivity_selftest: ',suffix:' cases passed',minimum:26}]};
 if(id==='settings.cas')return{schemaVersion:1,rules:[{line:'settings CAS selftest: PASS'},{line:'retired controller persona migration: PASS (12 target/overlay/startup/receipt cases)'}]};
 return undefined;
}
