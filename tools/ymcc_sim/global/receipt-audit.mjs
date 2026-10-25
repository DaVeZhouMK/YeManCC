import {BUNDLED_AUTHORITY_SOURCES,verifyBundledNodeReceipt} from './bundled-node-tool-authority.mjs';
import {verifySavedExecutionCompletion,completionProofMatchesContract,completionContractFingerprint,normalizeCompletionContract} from './execution-completion/index.mjs';
/** Read-only validation of existing run receipts against current source.
 * Caller order chooses the latest result for each case. No case is executed.
 * Absolute nested fixture hashes may resolve only inside that run's own case.
 */
import fs from 'node:fs';
import {isSleepEngineDllCase,auditSleepEngineDllPayload} from './consumer/sleep-engine/dll-payload-audit.mjs';
import path from 'node:path';
import {sourcePath,sha256File,checkStructuredEvidence,evaluateResult,MAX_ARTIFACT_INPUTS} from './runner.mjs';
const validId=value=>typeof value==='string'&&/^[a-z0-9][a-z0-9.-]{0,99}$/.test(value);
const validHash=value=>typeof value==='string'&&/^[a-f0-9]{64}$/i.test(value);
const validSource=value=>value&&typeof value==='object'&&typeof value.path==='string'&&validHash(value.sha256);
const inside=(root,target)=>{const rel=path.relative(root,target);return rel===''||(!rel.startsWith(`..${path.sep}`)&&rel!=='..'&&!path.isAbsolute(rel));};
/** Compare explicit path membership, preserving duplicate and provenance differences. */
function registeredArtifactMismatch(executed,declared,code){
 const scan=(rows,objects)=>{
  const paths=new Map(),duplicates=[],invalid=[];
  if(!Array.isArray(rows)||rows.length>MAX_ARTIFACT_INPUTS)return {paths,duplicates,invalid,schemaInvalid:true,count:Array.isArray(rows)?rows.length:null};
  rows.forEach((row,index)=>{
   const file=objects?row?.path:row;
   if(typeof file!=='string'||!path.isAbsolute(file)||/[\0\r\n]/.test(file)){invalid.push({index,path:typeof file==='string'?file:null});return;}
   const resolved=path.resolve(file),key=process.platform==='win32'?resolved.toLowerCase():resolved;
   if(paths.has(key))duplicates.push({path:file,firstPath:paths.get(key)});else paths.set(key,file);
  });
  return {paths,duplicates,invalid,schemaInvalid:false,count:rows.length};
 };
 const actual=scan(executed===undefined?[]:executed,true),expected=scan(declared===undefined?[]:declared,false);
 const missing=[...expected.paths].filter(([key])=>!actual.paths.has(key)).map(([,file])=>file);
 const extra=[...actual.paths].filter(([key])=>!expected.paths.has(key)).map(([,file])=>file);
 return missing.length||extra.length||actual.duplicates.length||expected.duplicates.length||actual.invalid.length||expected.invalid.length||actual.schemaInvalid||expected.schemaInvalid?{
  code,expectedCount:expected.count,executedCount:actual.count,missing,extra,
  duplicateExecuted:actual.duplicates,duplicateDeclared:expected.duplicates,
  invalidExecuted:actual.invalid,invalidDeclared:expected.invalid,
  executedSchemaInvalid:actual.schemaInvalid,declaredSchemaInvalid:expected.schemaInvalid
 }:null;
}
export const RECEIPT_AUDIT_LIMITS=Object.freeze({maxFiles:128,maxSingleBytes:32*1024*1024,maxTotalBytes:512*1024*1024});
function read(file,budget){
 const size=fs.statSync(file).size;
 if(size>RECEIPT_AUDIT_LIMITS.maxSingleBytes)throw Error('RECEIPT_SIZE_LIMIT');
 if(budget.used+size>budget.limit)throw Error('RECEIPT_TOTAL_SIZE_LIMIT');
 const bytes=fs.readFileSync(file);
 if(bytes.length>RECEIPT_AUDIT_LIMITS.maxSingleBytes)throw Error('RECEIPT_SIZE_LIMIT');
 if(budget.used+bytes.length>budget.limit)throw Error('RECEIPT_TOTAL_SIZE_LIMIT');
 budget.used+=bytes.length;
 return JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,''));
}
export function auditReceipts(receiptFiles,{repoRoot,maxTotalReceiptBytes=RECEIPT_AUDIT_LIMITS.maxTotalBytes}={}){
 const report={schemaVersion:1,kind:'ymcc-current-receipt-audit',generatedAt:new Date().toISOString(),status:'BLOCKED',mode:'read-only-no-execution',
  sourceRuns:[],cases:[],diagnostics:[],fullProductAcceptance:false,physicalHardwareAcceptance:false,binaryCompatibilityReexecuted:false,executedTests:0};
 report.readLimits={maxFiles:RECEIPT_AUDIT_LIMITS.maxFiles,maxSingleBytes:RECEIPT_AUDIT_LIMITS.maxSingleBytes,maxTotalBytes:maxTotalReceiptBytes};
 report.receiptBytesRead=0;
 if(!Array.isArray(receiptFiles)||!receiptFiles.length||receiptFiles.length>RECEIPT_AUDIT_LIMITS.maxFiles){report.diagnostics.push({code:'RECEIPT_LIST_REQUIRED_MAX_128'});return report;}
 if(!Number.isSafeInteger(maxTotalReceiptBytes)||maxTotalReceiptBytes<1||maxTotalReceiptBytes>RECEIPT_AUDIT_LIMITS.maxTotalBytes){report.diagnostics.push({code:'RECEIPT_TOTAL_SIZE_LIMIT_INVALID'});return report;}
 const budget={used:0,limit:maxTotalReceiptBytes},latest=new Map();
 for(const locator of receiptFiles){
  let file,value;
  try{file=path.resolve(locator);value=read(file,budget);report.receiptBytesRead=budget.used;
   if(!value||value.tool!=='ymcc_sim-global'||value.schemaVersion!==1||!['run','log-check'].includes(value.mode)||!Array.isArray(value.results)||!value.results.length)throw Error();
   const ids=new Set();
   for(const item of value.results){if(!validId(item?.id)||ids.has(item.id))throw Error();ids.add(item.id);}
  }catch(error){
   report.receiptBytesRead=budget.used;
   if(error?.message==='RECEIPT_TOTAL_SIZE_LIMIT'){report.diagnostics.push({code:'RECEIPT_TOTAL_SIZE_LIMIT',file:typeof locator==='string'?locator:null,maxBytes:budget.limit,bytesRead:budget.used});return report;}
   report.diagnostics.push({code:'RUN_RECEIPT_INVALID',file:typeof locator==='string'?locator:null});continue;
  }
  report.sourceRuns.push({file,sha256:sha256File(file),runId:value.runId,order:report.sourceRuns.length+1,originalVerdict:value.verdict});
  for(const item of value.results)latest.set(item.id,{item,root:path.dirname(file),runId:value.runId,hostPayload:value.hostPayload,candidateInputs:value.candidateInputs,executionRepoRoot:value.repoRoot});
 }
 for(const {item,root,runId,hostPayload,candidateInputs,executionRepoRoot}of latest.values()){
  const row={id:item.id,originalStatus:item.status,status:'BLOCKED',sourceRunId:runId,sourceMismatches:[],diagnostics:[],
   referenceClosure:item.referenceClosure??null,referenceInputs:item.referenceInputs??[],caseDir:path.join(root,'cases',item.id),
   execution:{source:item.source??null,scope:item.scope??null,completionProof:item.completionProof??null,sourceInputs:[...(Array.isArray(item.sourceInputs)?item.sourceInputs:[]),...(Array.isArray(item.dependencyInputs)?item.dependencyInputs:[])].map(row=>row?.path)}};
  report.cases.push(row);
  if(item.status!=='PASS'){row.status=item.status==='FAIL'||item.status==='TIMEOUT'?'FAIL':'BLOCKED';row.diagnostics.push({code:'PRIOR_CASE_NOT_PASS'});continue;}
  if(evaluateResult(item)!=='PASS'){row.status='FAIL';row.diagnostics.push({code:'CONTRADICTORY_PASS_RECEIPT'});continue;}
  if(!Array.isArray(item.sourceInputs)||(item.dependencyInputs!==undefined&&!Array.isArray(item.dependencyInputs))){row.diagnostics.push({code:'SOURCE_INPUT_SCHEMA_INVALID'});continue;}
  const inputs=[...(item.sourceInputs??[]),...(item.dependencyInputs??[])];
  if(inputs.some(input=>!validSource(input))){row.diagnostics.push({code:'SOURCE_INPUT_SCHEMA_INVALID'});continue;}
  if(!inputs.length){row.diagnostics.push({code:'REQUIRED_SOURCE_RECEIPT_MISSING'});continue;}
  for(const input of inputs){let actual=null;
   try{actual=sha256File(sourcePath(repoRoot,input.path));}catch{}
   if(!/^[a-f0-9]{64}$/i.test(input.sha256??'')||actual!==input.sha256.toLowerCase())row.sourceMismatches.push({path:input.path,expected:input.sha256??null,actual});
  }
  if(item.candidateArtifacts!==undefined){
    if(!Array.isArray(item.candidateArtifacts)||item.candidateArtifacts.length>MAX_ARTIFACT_INPUTS){row.diagnostics.push({code:'CANDIDATE_ARTIFACT_SCHEMA_INVALID'});continue;}
    for(const binary of item.candidateArtifacts){
      if(!binary||typeof binary.path!=='string'||!path.isAbsolute(binary.path)||/[\0\r\n]/.test(binary.path)||!/^[a-f0-9]{64}$/i.test(binary.sha256??'')){row.sourceMismatches.push({role:'explicit-candidate-artifact',path:binary?.path??null,expected:binary?.sha256??null,actual:null});continue;}
      let actual=null;try{actual=sha256File(binary.path);}catch{}
      if(actual!==binary.sha256.toLowerCase())row.sourceMismatches.push({path:binary.path,role:'explicit-candidate-artifact',expected:binary.sha256,actual});
    }
  }
  if(item.bundle!==undefined||item.bundledToolAuthority!==undefined||item.compilerKind==='typescript-esbuild-bundle'||/\.(?:[cm]?ts|tsx)$/.test(item.source??'')) {
    try {row.execution.bundledToolProof=verifyBundledNodeReceipt(item,repoRoot,row.caseDir,{limit:MAX_ARTIFACT_INPUTS});row.execution.explicitAdapterArtifacts=item.explicitAdapterArtifacts;}
    catch(error) {row.diagnostics.push({code:'BUNDLED_NODE_TOOL_AUTHORITY_INVALID',reason:String(error.message)});continue;}
  }
  if(isSleepEngineDllCase(item.id)) {
    try { row.payloadIdentity=auditSleepEngineDllPayload({item,caseDir:row.caseDir,hostPayload,candidateInputs,repoRoot,executionRepoRoot}); }
    catch(error) { row.diagnostics.push({code:'SLEEP_DLL_PAYLOAD_IDENTITY_INVALID',reason:error?.message??'UNREADABLE_EVIDENCE'});continue; }
  } else if(item.scope==='host-mock') {
    const binaries=hostPayload?.binaries;
    const required=['YeManFanHost.exe','YeManFanHost.dll'];
    if(typeof hostPayload?.root!=='string'||!path.isAbsolute(hostPayload.root)||!Array.isArray(binaries)||
       binaries.some(binary=>!binary||typeof binary.name!=='string'||(binary.sha256!==null&&!validHash(binary.sha256)))||
       new Set(binaries.map(binary=>binary.name)).size!==binaries.length||
       !required.every(name=>binaries.some(binary=>binary.name===name&&validHash(binary.sha256)))){
      row.diagnostics.push({code:'HOST_PAYLOAD_IDENTITY_MISSING'});continue;
    }
    for(const binary of binaries.filter(value=>['YeManFanHost.exe','YeManFanHost.dll','YeManFanBackend.dll','Microsoft.Windows.SDK.NET.dll','WinRT.Runtime.dll'].includes(value.name))){let actual=null;
      try{actual=sha256File(path.join(hostPayload.root,binary.name));}catch{}
      if(actual!==(binary.sha256===null?null:binary.sha256.toLowerCase()))row.sourceMismatches.push({path:binary.name,role:'explicit-host-binary',expected:binary.sha256,actual});
    }
  }
  if(row.sourceMismatches.length){row.status='STALE_SOURCE';continue;}
  row.execution.candidateArtifacts=(item.candidateArtifacts??[]).map(binary=>({path:binary.path,sha256:binary.sha256.toLowerCase()}));
  if(item.structuredEvidence!==undefined&&(!Array.isArray(item.structuredEvidence)||item.structuredEvidence.some(proof=>!proof||typeof proof.path!=='string'||!path.isAbsolute(proof.path)||!validHash(proof.sha256)))){row.diagnostics.push({code:'STRUCTURED_EVIDENCE_SCHEMA_INVALID'});continue;}
  const resultFiles=[];let invalidPath=false,evidenceChanged=false;
  for(const proof of item.structuredEvidence??[]){
   const candidate=path.resolve(proof.path);
   if(!inside(row.caseDir,candidate)||path.extname(candidate).toLowerCase()!=='.json'){invalidPath=true;break;}
   try{if(!inside(fs.realpathSync(row.caseDir),fs.realpathSync(candidate))){invalidPath=true;break;}}catch{invalidPath=true;break;}
   let actual=null;try{actual=sha256File(candidate);}catch{}
   if(actual!==proof.sha256.toLowerCase()||proof.failed===true){evidenceChanged=true;row.diagnostics.push({code:'STRUCTURED_EVIDENCE_CHANGED_SINCE_EXECUTION',path:candidate,expected:proof.sha256,actual});}
   resultFiles.push(path.relative(row.caseDir,candidate));
  }
  if(invalidPath){row.diagnostics.push({code:'NESTED_RECEIPT_OUTSIDE_OWN_CASE'});continue;}
  if(evidenceChanged){row.status='STALE_OR_INVALID_EVIDENCE';continue;}
  const proof=checkStructuredEvidence(row.caseDir,{resultFiles},repoRoot);
  row.structuredEvidence=proof.paths;
  if(proof.missing||proof.failed){row.status='STALE_OR_INVALID_EVIDENCE';row.diagnostics.push({code:proof.missing?'REQUIRED_RECEIPT_MISSING':'REQUIRED_RECEIPT_OR_PRODUCT_HASH_INVALID'});continue;}
  if(item.completionProof!==undefined){const completion=verifySavedExecutionCompletion(item,row.caseDir);if(completion.status!=='PASS'){row.status='STALE_OR_INVALID_EVIDENCE';row.diagnostics.push({code:completion.code});continue;}}
  row.status='PASS';
 }
 report.summary={cases:report.cases.length,currentPass:report.cases.filter(r=>r.status==='PASS').length,
  stale:report.cases.filter(r=>r.status.startsWith('STALE')).length,blocked:report.cases.filter(r=>r.status==='BLOCKED').length,
  failed:report.cases.filter(r=>r.status==='FAIL').length,referenceOnly:report.cases.filter(r=>r.referenceInputs.length>0).length};
 report.needsRerun=report.cases.filter(r=>r.status!=='PASS').map(r=>r.id);
 report.status=report.diagnostics.length||!report.cases.length?'BLOCKED':report.summary.failed?'FAIL':report.needsRerun.length?'REVALIDATION_REQUIRED':'CURRENT_RECEIPTS_VALID';
 report.selectionMeaning='Only supplied case receipts, latest in caller order; unexecuted references and full-product coverage remain unverified';
 return report;
}


/** Compare a read-only receipt audit with today's explicitly selected registry.
 * This does not execute or manufacture results for a newly registered test.
 */
export function auditRegisteredScope(report, adapters, {profile='full', domains=[], tests=[]}={}) {
 if(!report || !Array.isArray(report.cases) || !Array.isArray(report.diagnostics) || typeof report.status!=='string' ||
    (report.needsRerun!==undefined&&!Array.isArray(report.needsRerun)) ||
    !Array.isArray(adapters) || !['desktop','full','sleep'].includes(profile) || !Array.isArray(domains) || !Array.isArray(tests) ||
    domains.some(value=>typeof value!=='string'||!value) || tests.some(value=>typeof value!=='string'||!validId(value)))throw Error('REGISTERED_AUDIT_INPUT_INVALID');
 const priorNeeds=report.needsRerun??[];
 const priorSummary=report.summary??{cases:report.cases.length,currentPass:0,stale:0,blocked:0,failed:0,referenceOnly:0};
 const ids=new Set();
 for(const adapter of adapters){
  if(!adapter || !validId(adapter.id) || ids.has(adapter.id) || typeof adapter.domain!=='string' ||
     !Array.isArray(adapter.profiles) || adapter.profiles.some(value=>!['desktop','full','sleep'].includes(value)))throw Error('REGISTERED_AUDIT_SCHEMA_INVALID');
  ids.add(adapter.id);
  if(adapter.completionContract!==undefined)normalizeCompletionContract(adapter.completionContract);
 }
 const unknown=[...domains.filter(domain=>!adapters.some(row=>row.domain===domain)),...tests.filter(id=>!ids.has(id))];
 if(unknown.length)throw Error('UNKNOWN_SELECTION:'+unknown.join(','));
 const selected=adapters.filter(row=>row.profiles.includes(profile)&&(!domains.length||domains.includes(row.domain))&&(!tests.length||tests.includes(row.id)));
 const current=new Map();for(const row of report.cases){if(!validId(row?.id)||current.has(row.id))throw Error('REGISTERED_AUDIT_CASE_SCHEMA_INVALID');current.set(row.id,row);}
 const selectedMap=new Map(selected.map(row=>[row.id,row]));
 const cases=report.cases.map(row=>{
  const adapter=selectedMap.get(row.id);if(!adapter||row.status!=='PASS')return row;
  const differences=[];
  if(adapter.blockedReason)differences.push({code:'REGISTERED_PREREQUISITE_UNAVAILABLE',reason:adapter.blockedReason});
  if(adapter.kind==='typescript') {
    if(!row.execution?.bundledToolProof?.toolAuthorityVerified)differences.push({code:'REGISTERED_BUNDLED_AUTHORITY_NOT_EXECUTED'});
    for(const required of BUNDLED_AUTHORITY_SOURCES)if(!row.execution?.sourceInputs?.includes(required))differences.push({code:'REGISTERED_BUNDLED_IMPLEMENTATION_NOT_EXECUTED',path:required});


  }
  // Verified automatic TS tools and entry/map are not explicit registration members.
  const artifactMismatch=registeredArtifactMismatch(adapter.kind==='typescript'?row.execution?.explicitAdapterArtifacts:row.execution?.candidateArtifacts,adapter.artifactInputs,adapter.kind==='typescript'?'REGISTERED_EXPLICIT_ARTIFACTS_CHANGED':'REGISTERED_ARTIFACTS_CHANGED');
  if(artifactMismatch)differences.push(artifactMismatch);
   if(adapter.completionContract!==undefined&&!completionProofMatchesContract(row.execution?.completionProof,adapter.completionContract))differences.push({code:'REGISTERED_COMPLETION_CONTRACT_NOT_EXECUTED',expected:completionContractFingerprint(adapter.completionContract),executed:row.execution?.completionProof?.contractFingerprint??null});
  if(row.execution?.source!==adapter.path)differences.push({code:'REGISTERED_ENTRY_CHANGED',expected:adapter.path,executed:row.execution?.source??null});
  if(row.execution?.scope!==adapter.scope)differences.push({code:'REGISTERED_SCOPE_CHANGED',expected:adapter.scope,executed:row.execution?.scope??null});
  const inputs=new Set(Array.isArray(row.execution?.sourceInputs)?row.execution.sourceInputs:[]);
  for(const required of new Set([adapter.path,...(adapter.evidenceInputs??[])]))if(!inputs.has(required))differences.push({code:'REGISTERED_REQUIRED_SOURCE_NOT_EXECUTED',path:required});
  const evidence=new Set((row.structuredEvidence??[]).map(proof=>path.resolve(proof.path??'')));
  for(const required of adapter.resultFiles??[])if(!row.caseDir||!evidence.has(path.resolve(row.caseDir,required)))differences.push({code:'REGISTERED_REQUIRED_RESULT_NOT_RECORDED',path:required});
  return differences.length?{...row,status:'STALE_REGISTRATION',registrationMismatches:differences}:row;
 });
 current.clear();for(const row of cases)current.set(row.id,row);
 const missing=selected.filter(row=>!current.has(row.id)).map(row=>({id:row.id,domain:row.domain,path:row.path??null,status:'NOT_RUN',reason:'NO_EXECUTION_RECEIPT_FOR_CURRENT_REGISTERED_TEST'}));
 const notPassed=selected.filter(row=>current.has(row.id)&&current.get(row.id).status!=='PASS').map(row=>({id:row.id,status:current.get(row.id).status}));
 const complete=selected.length>0&&!missing.length&&!notPassed.length;
 let status=report.status;
 if(!selected.length)status=report.status==='FAIL'?'FAIL':'BLOCKED';
 else if(missing.length&&status==='CURRENT_RECEIPTS_VALID')status='REGISTERED_CASES_NOT_EXECUTED';
 else if(notPassed.length&&status==='CURRENT_RECEIPTS_VALID')status='REVALIDATION_REQUIRED';
 const diagnostics=[...report.diagnostics];
 if(!selected.length)diagnostics.push({code:'EMPTY_REGISTERED_SELECTION'});
 return {...report,status,diagnostics,cases,
  registeredScope:{profile,domains:[...new Set(domains)],tests:[...new Set(tests)],selectedCount:selected.length,
   ids:selected.map(row=>row.id),missing,notPassed,complete,
   meaning:'Only current registered tests in the explicit profile/selection. No tests execute during this audit. Passing this scope does not prove complete product or physical behavior.'},
  summary:{...priorSummary,currentPass:cases.filter(row=>row.status==='PASS').length,stale:cases.filter(row=>row.status.startsWith('STALE')).length,registeredSelected:selected.length,registeredMissing:missing.length,registeredNotPassed:notPassed.length},
  needsRerun:[...new Set([...priorNeeds,...cases.filter(row=>row.status==='STALE_REGISTRATION').map(row=>row.id),...missing.map(row=>row.id)])],
  selectionMeaning:'Supplied receipts remain latest in caller order; current registry selection is also checked for missing or nonpassing execution evidence.'};
}
