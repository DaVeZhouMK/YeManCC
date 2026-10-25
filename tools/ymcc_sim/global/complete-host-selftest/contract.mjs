import path from 'node:path';
import { createHash } from 'node:crypto';
export const ID = 'fan.complete-host-selftest';
export const HOST_ARGUMENTS = Object.freeze(['--self-test']);
export const TUPLE_FILES = Object.freeze({hostDllSha256:'YeManFanHost.dll',backendSha256:'YeManFanBackend.dll',apphostSha256:'YeManFanHost.exe',manifestSha256:'YeManFanHost.payload.json'});
export const digest = bytes => createHash('sha256').update(bytes).digest('hex');
const goodHash=x=>typeof x==='string'&&/^[a-f0-9]{64}$/.test(x);
const plain=x=>x!==null&&typeof x==='object'&&!Array.isArray(x);
const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
function requireValue(ok,code){if(!ok)throw Error(code);}
export function validateContract(value){
 requireValue(plain(value)&&value.schemaVersion===1&&value.kind==='ymcc-complete-host-selftest-v1','CONTRACT_SCHEMA_INVALID');
 requireValue(same(value.hostArguments,HOST_ARGUMENTS),'ONLY_COMPLETE_SELFTEST_ARGUMENT_ALLOWED');
 requireValue(Number.isInteger(value.hostTimeoutMs)&&value.hostTimeoutMs>=1000&&value.hostTimeoutMs<=180000,'HOST_TIMEOUT_INVALID');
 requireValue(Number.isInteger(value.maxLogBytes)&&value.maxLogBytes>=1024&&value.maxLogBytes<=16*1024*1024,'LOG_LIMIT_INVALID');
 requireValue(plain(value.expectedTuple)&&Object.keys(TUPLE_FILES).every(k=>goodHash(value.expectedTuple[k])),'EXPLICIT_PAYLOAD_TUPLE_REQUIRED');
 requireValue(Array.isArray(value.payloadFiles)&&value.payloadFiles.length>=4&&value.payloadFiles.length<=64,'PAYLOAD_FILE_CONTRACT_REQUIRED');
 const names=new Set();
 for(const row of value.payloadFiles){requireValue(plain(row)&&typeof row.name==='string'&&/^[a-z0-9][a-z0-9._-]*$/i.test(row.name)&&goodHash(row.sha256)&&!names.has(row.name.toLowerCase()),'PAYLOAD_FILE_CONTRACT_INVALID');names.add(row.name.toLowerCase());}
 for(const [key,name] of Object.entries(TUPLE_FILES))requireValue(value.payloadFiles.some(r=>r.name===name&&r.sha256===value.expectedTuple[key]),'PAYLOAD_TUPLE_FILE_DISAGREES');
 requireValue(plain(value.tools)&&goodHash(value.tools.nodeSha256)&&goodHash(value.tools.jobHelperSha256),'REVIEWED_TOOL_IDENTITIES_REQUIRED');
 return value;
}
function findObjectEnd(text,start){let depth=0,string=false,escape=false;for(let i=start;i<text.length;i++){const ch=text[i];if(string){if(escape)escape=false;else if(ch==='\\')escape=true;else if(ch==='"')string=false;continue;}if(ch==='"')string=true;else if(ch==='{')depth++;else if(ch==='}'&&--depth===0)return i+1;}return null;}
/** Recognize the genuine final Main result, never a diagnostic or a subset count. */
export function parseCompleteResult(bytes,{maxLogBytes=16*1024*1024}={}){
 const data=Buffer.isBuffer(bytes)?bytes:Buffer.from(bytes);const issues=[];
 if(data.length>maxLogBytes)return {result:null,checksPassed:null,checkCountObserved:false,issues:['STDOUT_LOG_SIZE_LIMIT']};
 const text=data.toString('utf8').replace(/^\uFEFF/,'');const markers=[];const starts=/^[\t ]*\{/gm;let match;
 while((match=starts.exec(text))){const start=text.indexOf('{',match.index),end=findObjectEnd(text,start);if(end===null)continue;try{const value=JSON.parse(text.slice(start,end));if(plain(value)&&['ok','checks','finalState','handshake','openState'].every(k=>Object.hasOwn(value,k)))markers.push({value,end});}catch{}}
 if(markers.length!==1)return {result:null,checksPassed:null,checkCountObserved:false,issues:[markers.length?'AMBIGUOUS_COMPLETE_SELFTEST_RESULT':'COMPLETE_SELFTEST_RESULT_NOT_OBSERVED']};
 const {value:result,end}=markers[0];
 if(text.slice(end).trim())issues.push('TRAILING_OUTPUT_AFTER_SELFTEST_RESULT');
 if(result.ok!==true)issues.push('COMPLETE_SELFTEST_OK_NOT_TRUE');
 const observed=Array.isArray(result.checks)&&result.checks.length>0&&result.checks.every(x=>typeof x==='string'&&x.trim().length>0);
 if(!observed)issues.push('GENUINE_CHECK_ARRAY_REQUIRED');
 if(!plain(result.finalState)||!['hardwareWritesEnabled','hardwareWritesObserved','hardwareWrites','hardwareCapable'].every(k=>result.finalState[k]===false))issues.push('FINAL_HARDWARE_FLAGS_NOT_PROVEN_FALSE');
 if(!plain(result.handshake)||result.handshake.hostMode!=='safe-no-hardware'||result.handshake.hardwareWritesEnabled!==false||result.handshake.hardwareWritesObserved!==false)issues.push('SAFE_HANDSHAKE_NOT_OBSERVED');
 if(!plain(result.openState))issues.push('GENUINE_OPEN_STATE_NOT_OBSERVED');
 return {result,checksPassed:observed?result.checks.length:null,checkCountObserved:observed,issues};
}
export function assessHostOutcome({contract,execution,stdout,stderr}){
 validateContract(contract);const parsed=parseCompleteResult(stdout,contract);const issues=[...parsed.issues];
 if(!plain(execution)||!same(execution.arguments,HOST_ARGUMENTS))issues.push('FIXED_SELFTEST_ARGUMENTS_NOT_OBSERVED');
 if(!Number.isInteger(execution?.pid)||execution.pid<=0)issues.push('HOST_PID_NOT_OBSERVED');
 if(execution?.processExitCode!==0)issues.push('HOST_NONZERO_OR_UNKNOWN_EXIT');
 if(execution?.naturalExit!==true||execution?.timedOut!==false||execution?.forcedTermination!==false)issues.push('HOST_NATURAL_EXIT_NOT_PROVEN');
 if(!Number.isFinite(Date.parse(execution?.startedUtc))||!Number.isFinite(Date.parse(execution?.finishedUtc))||Date.parse(execution.finishedUtc)<Date.parse(execution.startedUtc))issues.push('HOST_TIME_RANGE_INVALID');
 for(const key of Object.keys(TUPLE_FILES))if(execution?.tuple?.[key]!==contract.expectedTuple[key])issues.push('PAYLOAD_TUPLE_MISMATCH:'+key);
 for(const key of ['realBackendRequested','hardwareWritesRequested','realTokenFileRequested','installedApplicationMutated','candidateInstalled'])if(execution?.[key]!==false)issues.push('SOFTWARE_SCOPE_NOT_PROVEN:'+key);
 if(Buffer.byteLength(stderr)>contract.maxLogBytes)issues.push('STDERR_LOG_SIZE_LIMIT');
 if(/Unhandled exception\.|FATAL(?:\s|:)|FAN-934: victim route/i.test(Buffer.from(stderr).toString('utf8')))issues.push('ORIGINAL_HOST_EXCEPTION_OBSERVED');
 return {...parsed,status:issues.length?'FAIL':'PASS',issues:[...new Set(issues)]};
}
export function assessAdapterExecution({contract,execution,stdout,stderr}){
 validateContract(contract);
 if(execution?.started!==true)return {status:'BLOCKED',issues:[execution?.blockReason??'HOST_EXECUTION_NOT_OBSERVED'],checksPassed:null,checkCountObserved:false,result:null};
 const outcome=assessHostOutcome({contract,execution,stdout,stderr}),issues=[...outcome.issues];
 if(execution.mode!=='complete-host-selftest'||execution.guard?.globalRunner!==true||execution.guard?.jobMember!==true||execution.guard?.helperSha256!==contract.tools.jobHelperSha256)issues.push('EXISTING_GUARDED_JOB_NOT_PROVEN');
 if(!path.isAbsolute(execution.payloadRoot??'')||execution.executable!==path.join(execution.payloadRoot??'','YeManFanHost.exe'))issues.push('EXPLICIT_PAYLOAD_EXECUTABLE_REQUIRED');
 if(!Array.isArray(execution.noConsumers)||execution.noConsumers.length!==2||!same(execution.noConsumers.map(r=>r.phase),['initial','immediately-before-start'])||execution.noConsumers.some(r=>r.status!=='PASS'||r.consumerCount!==0))issues.push('BOTH_NO_CONSUMERS_CHECKS_REQUIRED');
 const expected=[...contract.payloadFiles].sort((a,b)=>a.name.localeCompare(b.name,'en'));
 for(const key of ['payloadBefore','payloadAfter']){const rows=execution[key];if(!Array.isArray(rows)||!same([...rows].sort((a,b)=>a.name.localeCompare(b.name,'en')),expected))issues.push('PAYLOAD_CLOSURE_NOT_STABLE:'+key);}
 if(execution.outputComplete!==true||execution.stdoutSha256!==digest(stdout)||execution.stderrSha256!==digest(stderr))issues.push('RAW_OUTPUT_CAPTURE_IDENTITY_INVALID');
 if(execution.nodeSha256!==contract.tools.nodeSha256)issues.push('REVIEWED_NODE_IDENTITY_MISMATCH');
 return {...outcome,status:issues.length?'FAIL':'PASS',issues:[...new Set(issues)]};
}