import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {extract} from '../../../fixtures/legion_motion_consumer/extract_main.mjs';
import {closeBrace} from '../../../fixtures/hc_remaining_model_capabilities/expectations.mjs';
import {hashFile,readJson,ownedFile,ownedTree,inside} from './evidence.mjs';
import {validHash,validCount} from './contracts.mjs';

export const MATRIX_CHECKER='tools/hc_motion_matrix_binding_selftest.mjs';
export const MATRIX_PROBE='tools/fixtures/hc_model_motion_consumer/matrix_binding_probe.cpp';
export const MATRIX_BUDGET_MS=240000;
export const MATRIX_CHILD_TIMEOUT_MS=60000;
export const MATRIX_OUTPUT_LIMIT=2*1024*1024;
export const MATRIX_FAULTS=Object.freeze([
 {id:'rollback-to-table-search',kind:'performance-regression',expected:'MATRIX_LOOKUP_REMOVAL'},
 {id:'live-reviewed-bound-removed',kind:'semantic-source-fault',expected:'MATRIX_OUTPUT_EQUIVALENCE'},
 {id:'wrong-registry-kind',kind:'semantic-source-fault',expected:'MATRIX_OUTPUT_EQUIVALENCE'},
 {id:'gyro-transform-skipped',kind:'semantic-source-fault',expected:'MATRIX_OUTPUT_EQUIVALENCE'},
]);
// Preserve the complete reviewed 574-member allowlist, including prerequisites.
// Only sourceDependencies entries below count as observed includes in this run.
export const MOTION_TOOL_ADDITIONS='tools/ymcc_sim/global/motion-consumer-signals/toolchain-additions.json';
export const MOTION_TOOL_PARENT_SHA256='c31d72912fb8523133615f724d7c899ed53e3fb7818cb6f38abfc797c1b0c73d';
export const MOTION_MD_PREREQUISITES=Object.freeze([
  {
    "path": "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/lib/x64/msvcrt.lib",
    "sha256": "07670951a39739b6d1b50421ae61707ef23c8dff50cdf35f99408711478f3a7c"
  },
  {
    "path": "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/lib/x64/msvcrtd.lib",
    "sha256": "7bb5b7a7d2042c2eb9f0c93b95630dc6d011319e8af133ab759d957318fd47ce"
  },
  {
    "path": "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/lib/x64/msvcprt.lib",
    "sha256": "59d3ab1065f0a14bd9b27debef4008a91eafbc7411290013f61091bf12dfca0f"
  },
  {
    "path": "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/lib/x64/msvcprtd.lib",
    "sha256": "dd585a0b1c98c865dcea6bec8533dc32c85774ba34aa0539d36c9196199305f8"
  },
  {
    "path": "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/lib/x64/vcruntime.lib",
    "sha256": "1acbd98b43a4be8373e25c65dcae4412f36f6111d6b64e47e4a2a97a1d16b27b"
  },
  {
    "path": "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/lib/x64/vcruntimed.lib",
    "sha256": "d4185af4ea776f639deda9e413ec5939dd84d68cf5138eb03eccde20775d02f3"
  },
  {
    "path": "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/ucrt/x64/ucrt.lib",
    "sha256": "7ef4eac926bf597d2f243f16cdfed7e0db22cb3ca34a1d7e088a84c994a03d66"
  },
  {
    "path": "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/ucrt/x64/ucrtd.lib",
    "sha256": "0f7f2f05de9b472d3447aca287b8ef908a22f2997282a0fc799eadcb921f6a9c"
  }
].map(row=>Object.freeze(row)));
export const MATRIX_TOOL_MANIFESTS=Object.freeze([
 MOTION_TOOL_ADDITIONS,
 'tools/ymcc_sim/global/marker-ownership-consumer/toolchain-inputs.json',
 'tools/ymcc_sim/global/sleep-single-pid-freeze-consumer/toolchain-inputs.json',
 'tools/ymcc_sim/global/sleep-game-pause-consumer/toolchain-inputs.json',
 'tools/ymcc_sim/global/sleep-non-user-wake-consumer/toolchain-inputs.json',
 'tools/ymcc_sim/global/sleep-native-recovery-consumer/toolchain-inputs.json',
]);
export const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const key=file=>path.resolve(file).toLowerCase();
const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const need=(ok,code)=>{if(!ok)throw Error(code);};
const object=v=>v!==null&&typeof v==='object'&&!Array.isArray(v);
/** Exact parent-approved MD/MDd prerequisites; no directory authorization. */
export function validateMotionToolAdditions(spec){
 need(object(spec)&&spec.schemaVersion===1,'MATRIX_MOTION_TOOL_SCHEMA');
 need(Object.keys(spec).sort().join(',')==='additional,schemaVersion,sharedManifest,sharedManifestSha256','MATRIX_MOTION_TOOL_FIELDS');
 need(spec.sharedManifest===MATRIX_TOOL_MANIFESTS[1]&&spec.sharedManifestSha256===MOTION_TOOL_PARENT_SHA256,'MATRIX_MOTION_TOOL_PARENT_PIN');
 need(Array.isArray(spec.additional)&&spec.additional.length===MOTION_MD_PREREQUISITES.length,'MATRIX_MOTION_TOOL_EIGHT_REQUIRED');
 const expected=new Map(MOTION_MD_PREREQUISITES.map(row=>[key(row.path),row.sha256])),seen=new Set();
 for(const row of spec.additional){
  need(object(row)&&Object.keys(row).sort().join(',')==='path,sha256'&&typeof row.path==='string'&&path.isAbsolute(row.path)&&validHash(row.sha256)&&!seen.has(key(row.path))&&expected.get(key(row.path))===row.sha256,'MATRIX_MOTION_TOOL_MEMBER');seen.add(key(row.path));
 }
 return {schemaVersion:1,prerequisites:8,observedLinkInputs:false};
}
export function matrixToolchainInputs(repo){
 const files=new Map();let base;
 for(let i=0;i<MATRIX_TOOL_MANIFESTS.length;i++){
  const spec=readJson(path.join(repo,MATRIX_TOOL_MANIFESTS[i]));
  need(spec.schemaVersion===1,'MATRIX_TOOL_MANIFEST_SCHEMA');
  if(i===0)validateMotionToolAdditions(spec);
  if(i+1<MATRIX_TOOL_MANIFESTS.length){
   need(spec.sharedManifest===MATRIX_TOOL_MANIFESTS[i+1],'MATRIX_TOOL_MANIFEST_CHAIN');
   if(spec.sharedManifestSha256!==undefined)need(hashFile(path.join(repo,spec.sharedManifest))===spec.sharedManifestSha256,'MATRIX_TOOL_MANIFEST_HASH');
  }else{need(!spec.sharedManifest&&Array.isArray(spec.files),'MATRIX_TOOL_MANIFEST_BASE');base=spec;}
  for(const row of [...(spec.files??[]),...(spec.additional??[])]){
   need(object(row)&&typeof row.path==='string'&&path.isAbsolute(row.path)&&!/[\0\r\n]/.test(row.path)&&validHash(row.sha256),'MATRIX_TOOL_MEMBER_INVALID');
   const previous=files.get(key(row.path));need(!previous||previous.sha256===row.sha256,'MATRIX_TOOL_MEMBER_CONFLICT');files.set(key(row.path),row);
  }
 }
 need(files.size>=574,'MATRIX_COMPLETE_TOOL_ALLOWLIST_REQUIRED');
 need(files.size===582,'MATRIX_MOTION_COMPLETE_TOOL_ALLOWLIST_REQUIRED');
 return {files:[...files.values()],nodeExecutable:base.nodeExecutable,cmdExecutable:base.cmdExecutable,vcvars64:base.vcvars64,declaredOnly:true};
}
export function assertMatrixProcess(call){
 need(object(call)&&Number.isSafeInteger(call.pid)&&call.pid>0&&call.exitCode===0&&call.timedOut===false&&call.signal===null&&call.launchError===null&&call.outputTruncated===false&&Number.isFinite(call.durationMs)&&call.durationMs>=0,'MATRIX_CHECKER_PROCESS_FAILED');
}
export function matrixCheckerArguments({repo,out,source,generated,fault}){
 const args=[path.join(repo,MATRIX_CHECKER),'--repo',repo,'--fixture',path.join(repo,MATRIX_PROBE),'--source',source,'--out',out];
 if(fault){need(MATRIX_FAULTS.some(x=>same(x,fault))&&!generated,'MATRIX_FAULT_INVOCATION_INVALID');args.push('--profiles','Release','--expect-failure',fault.expected);}
 else{need(typeof generated==='string','MATRIX_POSITIVE_GENERATED_REQUIRED');args.push('--generated',generated,'--profiles','Release,Checked');}
 return args;
}
/** Pure byte controls; a schema sample or generated mutation is never execution evidence. */
export function createMatrixFaults(sourceBytes,fixtureBytes){
 const text=sourceBytes.toString('utf8');need(Buffer.from(text).equals(sourceBytes),'MATRIX_SOURCE_UTF8_REQUIRED');
 const entry=extract(text,'inputCaptureApplyHcMatrixCandidate'),before=entry.code;
 const fixture=fixtureBytes.toString('utf8'),frozen=extract(fixture,'inputCaptureApplyHcMatrixCandidate').code;
 const pin=/Baseline function SHA256: ([a-f0-9]{64})/.exec(fixture)?.[1];
 need(validHash(pin)&&sha(frozen.replaceAll('\r\n','\n'))===pin,'MATRIX_FROZEN_BODY_CHANGED');
 const newline=before.includes('\r\n')?'\r\n':'\n',rollback=frozen.replaceAll('\r\n','\n').replaceAll('\n',newline);
 const unique=(from,to)=>{need(before.split(from).length===2,'MATRIX_FAULT_HUNK_NOT_UNIQUE');return before.replace(from,to);};
 // The rollback is the one exact table-lookup hunk; require the entire resulting
 // function to equal the independently frozen pre-D14 complete method.
 const from='ymcc::capabilities::motion::consume(g_hcMotionSelector.unifiedBinding,'+newline+'        ymcc::capabilities::motion::Kind::Reviewed, ymcc::hc_model_motion::recipes)';
 const bodies=[unique(from,'ymcc::hc_model_motion::find(g_machineIdentity.deviceClass)'),unique(' && inputCaptureNewHcMotionSelectorBound()',''),unique('motion::Kind::Reviewed, ymcc::hc_model_motion::recipes','motion::Kind::Legacy, ymcc::hc_model_motion::recipes'),unique('if (!gyroId.empty())','if (false && !gyroId.empty())')];
 need(bodies[0]===rollback,'MATRIX_ROLLBACK_NOT_FROZEN_BODY');
 const prefix=sourceBytes.subarray(0,entry.startUtf8Byte),suffix=sourceBytes.subarray(entry.endUtf8ByteExclusive);
 return MATRIX_FAULTS.map((fault,i)=>{
  const modified=Buffer.from(bodies[i]);need(!modified.equals(Buffer.from(before)),'MATRIX_FAULT_UNCHANGED');
  const bytes=Buffer.concat([prefix,modified,suffix]);const reverse=Buffer.concat([bytes.subarray(0,prefix.length),Buffer.from(before),bytes.subarray(prefix.length+modified.length)]);
  need(reverse.equals(sourceBytes),'MATRIX_FAULT_REVERSE_FAILED');
  return {...fault,bytes,proof:{sourceBeforeSha256:sha(sourceBytes),sourceAfterSha256:sha(bytes),functionBeforeSha256:sha(before),functionAfterSha256:sha(modified),prefixSha256:sha(prefix),suffixSha256:sha(suffix),startUtf8Byte:entry.startUtf8Byte,endUtf8ByteExclusive:entry.endUtf8ByteExclusive,reverseSha256:sha(reverse),frozenOriginalFunctionSha256:pin}};
 });
}
/** Structural contract only; the runner separately requires natural child exit and real files. */
export function validateMatrixReport(report,{source,sourceSha256,fault}={}){
 const issues=[];const add=code=>{if(!issues.includes(code))issues.push(code);};
 if(!object(report)||report.schemaVersion!==1||report.status!=='PASS'||typeof report.source!=='string'||key(report.source)!==key(source??'')||report.sourceSha256!==sourceSha256||!validHash(sourceSha256)||report.expectedSemanticFailure!==(fault?.expected??null))add('MATRIX_SOURCE_OR_RESULT_INVALID');
 const profiles=fault?['Release']:['Release','Checked'],configs=report?.configurations;
 if(!Array.isArray(configs)||configs.length!==profiles.length||!same(configs.map(c=>c?.profile).sort(),[...profiles].sort()))add('MATRIX_CONFIGURATIONS_INVALID');
 for(const config of Array.isArray(configs)?configs:[]){
  if(config?.buildExit!==0||config.executionExit!==(fault?1:0)||config.accepted!==true||!validHash(config.executableSha256))add('MATRIX_NATIVE_EXECUTION_INVALID');
  const r=config?.result;
  if(!object(r)||!validCount(r.checks)||r.checks===0)add('MATRIX_NATIVE_ASSERTIONS_REQUIRED');
  if(fault){if(r?.status!=='FAIL'||r?.error!==fault.expected||typeof r?.scenario!=='string'||!r.scenario||!validCount(r.transforms)||!validCount(r.windows))add('MATRIX_EXACT_SEMANTIC_FAULT_REQUIRED');}
  else{
   if(r?.status!=='PASS'||r.error!==undefined||['checks','matrixTransformPairs','busWindowPairs','admitted','rejected','oldProcessCalls','newProcessCalls','oldMatrixReviewedFindCalls','oldMatrixStringComparisons','oldBusReviewedFindCalls','newBusReviewedFindCalls','distinctResolvedClasses','distinctBoundClasses'].some(n=>!validCount(r[n])||(n!=='newBusReviewedFindCalls'&&r[n]===0))||r.newMatrixReviewedFindCalls!==0||r.newMatrixStringComparisons!==0||r.oldProcessCalls!==r.newProcessCalls||r.admitted+r.rejected!==r.busWindowPairs||r.busWindowPairs!==r.matrixTransformPairs*8||r.oldBusReviewedFindCalls<=r.newBusReviewedFindCalls||r.fullBusExecuted!==false||r.realDevicesAccessed!==false||r.physicalAcceptance!==false||r.wholeProcessCpuReduction!==null)add('MATRIX_EQUIVALENCE_AND_COST_REQUIRED');
   if(!object(r?.states)||!same(Object.keys(r.states).sort(),Array.from({length:17},(_,i)=>String(i)).sort())||Object.values(r.states).some(n=>!validCount(n)||n===0)||Object.values(r.states).reduce((n,v)=>n+v,0)!==r.matrixTransformPairs)add('MATRIX_ALL_STATES_REQUIRED');
  }
  if(!Array.isArray(config?.actualIncludes)||!config.actualIncludes.length||config.actualIncludes.length>4096)add('MATRIX_ACTUAL_INCLUDES_REQUIRED');
 }
 if(!fault&&Array.isArray(configs)&&configs.length===2&&!same(configs[0]?.result,configs[1]?.result))add('MATRIX_CONFIGURATION_RESULTS_DIFFER');
 const instrumentation=report?.instrumentation;
 const omitted=instrumentation?.omittedEntry;
 if(instrumentation?.unusedFullSuiteEntryOmitted!==true||!object(omitted)||omitted.symbol!=='main'||omitted.references!==1||!validCount(omitted.startUtf8Byte)||!validCount(omitted.endUtf8ByteExclusive)||!validCount(omitted.bytes)||!omitted.bytes||omitted.endUtf8ByteExclusive-omitted.startUtf8Byte!==omitted.bytes||['sha256','keptPrefixSha256','keptSuffixSha256','generatedConsumerSha256'].some(n=>!validHash(omitted[n])))add('MATRIX_UNCALLED_HARNESS_PROOF_REQUIRED');
 if(!object(instrumentation)||['productionMatrixAndBusBodiesUnmodified','logicalEntryAndComparisonCountersOnly','originalLookupAlgorithmPreserved'].some(n=>instrumentation[n]!==true)||['lookupHeaderOriginalSha256','lookupHeaderInstrumentedSha256','immutableOriginalComparisonSha256'].some(n=>!validHash(instrumentation[n]))||instrumentation.lookupHeaderOriginalSha256===instrumentation.lookupHeaderInstrumentedSha256)add('MATRIX_INSTRUMENTATION_INVALID');
 if(!Array.isArray(report?.inputs)||!report.inputs.length||report.inputs.length>128||!Array.isArray(report?.drift)||report.drift.length||report?.fullBusExecuted!==false||report?.hardwareIo!==false||report?.physicalAcceptance!==false||report?.wholeProcessCpuReduction!==null)add('MATRIX_SCOPE_OR_DRIFT_INVALID');
 return {status:issues.length?'FAIL':'PASS',structuralValidationOnly:true,issues};
}
function pinnedMap(rows){const map=new Map();for(const row of rows){need(row&&typeof row.path==='string'&&path.isAbsolute(row.path)&&validHash(row.sha256),'MATRIX_PIN_INVALID');const previous=map.get(key(row.path));need(!previous||previous.sha256===row.sha256,'MATRIX_PIN_CONFLICT');map.set(key(row.path),row);}return map;}
function checkFile(file,expected){need(typeof file==='string'&&path.isAbsolute(file)&&validHash(expected),'MATRIX_FILE_IDENTITY_REQUIRED');let cursor=path.resolve(file);for(;;){need(!fs.lstatSync(cursor).isSymbolicLink(),'MATRIX_REPARSE_INPUT');const parent=path.dirname(cursor);if(parent===cursor)break;cursor=parent;}need(fs.statSync(file).isFile()&&hashFile(file)===expected,'MATRIX_FILE_HASH_MISMATCH');}
/** Exact membership: declared directory roots never authorize an include. */
export function verifyMatrixIncludes(rows,dependency,{sourceFile,sourceInputs,toolInputs,ownedInputs}){
 const expected=[dependency?.Data?.Source,...(dependency?.Data?.Includes??[])];
 need(expected.length>1&&expected.length===rows.length&&expected.every((file,i)=>typeof file==='string'&&path.isAbsolute(file)&&typeof rows[i]?.file==='string'&&key(file)===key(rows[i].file))&&key(expected[0])===key(sourceFile),'MATRIX_DEPENDENCY_REPORT_DIFFERS');
 const declared=pinnedMap([...sourceInputs,...toolInputs,...ownedInputs]),observed=[];
 for(const row of rows){
  need(object(row)&&typeof row.file==='string'&&path.isAbsolute(row.file)&&validHash(row.sha256),'MATRIX_INCLUDE_ROW_INVALID');
  const pin=declared.get(key(row.file));if(!pin||pin.sha256!==row.sha256)throw Object.assign(Error('MATRIX_UNDECLARED_COMPILER_INPUT'),{evidence:{file:row.file,reportedSha256:row.sha256,declaredSha256:pin?.sha256??null}});checkFile(row.file,row.sha256);
  observed.push({path:row.file,sha256:row.sha256,role:toolInputs.some(p=>key(p.path)===key(row.file))?'actual-tool-header':ownedInputs.some(p=>key(p.path)===key(row.file))?'actual-owned-source':'actual-repository-header'});
 }
 return observed;
}
// Rebuild only the checker instrumentation and entry wrapper, never product logic.
// This binds the compiled probe to the verified generator and unchanged fixture.
function expectedProbe(generated,fixture){
 let code=fs.readFileSync(ownedFile(generated,'motion_consumer.cpp'),'utf8');
 const ns=code.indexOf('namespace candidate {'),end=closeBrace(code,code.indexOf('{',ns));need(ns>=0,'MATRIX_CANDIDATE_NAMESPACE');const body=code.slice(ns,end+1);
 const matches=[...body.matchAll(/^static ProbeMotionResult probeActive[^\n]*\) \{/gm)];need(matches.length===1,'MATRIX_ACTIVE_PREFIX');const start=matches[0].index,open=start+matches[0][0].lastIndexOf('{'),active=body.slice(start,closeBrace(body,open)+1);
 const proof=fs.readFileSync(fixture,'utf8');need(proof.split('@@BASELINE_ACTIVE_PROBE@@').length===2&&code.split('int main(int argc,char** argv)').length===2,'MATRIX_PROBE_ENTRY');
 const marker='int main(int argc,char** argv)',first=code.indexOf(marker);need(first>=0&&code.indexOf(marker,first+1)<0&&(code.match(/\bmain\s*\(/g)??[]).length===1,'MATRIX_SINGLE_UNCALLED_HARNESS_MAIN_REQUIRED');
 const stop=closeBrace(code,code.indexOf('{',first))+1,prefix=code.slice(0,first),suffix=code.slice(stop),omitted=code.slice(first,stop);
 const omittedEntry={symbol:'main',startUtf8Byte:Buffer.byteLength(prefix),endUtf8ByteExclusive:Buffer.byteLength(code.slice(0,stop)),sha256:sha(omitted),bytes:Buffer.byteLength(omitted),keptPrefixSha256:sha(prefix),keptSuffixSha256:sha(suffix),generatedConsumerSha256:sha(code),references:1};
 return {code:prefix+'/* Matrix TU omits the uncalled full-suite entry only. */'+suffix+'\n'+proof.replace('@@BASELINE_ACTIVE_PROBE@@',active),omittedEntry};
}
function instrumentHeader(before){
 const start=before.indexOf('inline constexpr const Recipe* find(');need(start>=0&&before.indexOf('inline constexpr const Recipe* find(',start+1)<0,'MATRIX_FIND_DEFINITION');const end=closeBrace(before,before.indexOf('{',start)),find=before.slice(start,end+1),open=find.indexOf('{');need(find.split('recipe.deviceClass == deviceClass').length===2,'MATRIX_FIND_COMPARISON');
 const observed=(find.slice(0,open+1)+'\n    if(!std::is_constant_evaluated())++matrixProbeFindCalls;'+find.slice(open+1)).replace('recipe.deviceClass == deviceClass','matrixProbeObservedEqual(recipe.deviceClass,deviceClass)');
 const counters='inline unsigned long long matrixProbeFindCalls=0,matrixProbeComparisons=0;\ninline constexpr bool matrixProbeObservedEqual(std::string_view left,std::string_view right) noexcept {if(!std::is_constant_evaluated())++matrixProbeComparisons;return left==right;}\n';
 return before.replace('#include <string_view>','#include <string_view>\n#include <type_traits>').replace(find,counters+observed);
}
export function verifyMatrixEvidence({repo,out,generated,source,sourceSha256,sourceInputs,externalInputs,toolInputs,fault,generatedClosure,oracleSha256,checkerPid}){
 const resultFile=ownedFile(out,'result.json'),report=readJson(resultFile),validation=validateMatrixReport(report,{source,sourceSha256,fault});need(validation.status==='PASS','MATRIX_STRUCTURED_REPORT_REJECTED');checkFile(source,sourceSha256);
 const allowed=pinnedMap([...sourceInputs,...externalInputs,...toolInputs,{path:source,sha256:sourceSha256}]);
 const boundFile=ownedFile(generated,'source-bindings.json'),bound=readJson(boundFile);
 if(fault)ownedFile(out,'generate.log');
 need(key(bound.out??'')===key(generated)&&key(bound.source?.path??'')===key(source)&&bound.source?.sha256===sourceSha256&&bound.mode==='candidate'&&bound.status===(fault?'generated-only':'pass'),'MATRIX_GENERATED_SOURCE_IDENTITY');
 for(const row of ['source','before','historical','capabilityBefore','legacyCatalog','legacyOracle','catalog','msi','runner'].map(n=>bound[n]).concat(bound.fixtures??[]))need(row&&typeof row.path==='string'&&allowed.get(key(row.path))?.sha256===row.sha256,'MATRIX_UNRECORDED_GENERATOR_INPUT');
 checkFile(ownedFile(generated,'motion_consumer.cpp'),bound.generatedCppSha256);checkFile(ownedFile(generated,'expectations.json'),bound.oracleSha256);
 need(validHash(oracleSha256)&&bound.oracleSha256===oracleSha256,'MATRIX_ORACLE_DIFFERS_FROM_ORIGINAL_PRODUCER');
 const oracle=readJson(ownedFile(generated,'expectations.json'));
 need(Array.isArray(oracle.identities)&&Array.isArray(oracle.legacy?.cases),'MATRIX_GENERATED_ORACLE_INVALID');
 if(!fault){const perState=(oracle.identities.length+oracle.legacy.cases.length+4)*2*3;for(const c of report.configurations)need(c.result.matrixTransformPairs===perState*17&&Object.values(c.result.states).every(n=>n===perState),'MATRIX_COMPLETE_ORACLE_GRID_REQUIRED');}
 if(generatedClosure)for(const row of generatedClosure)checkFile(row.path,row.sha256);
 need(report.sourceBindings?.file&&key(report.sourceBindings.file)===key(boundFile)&&report.sourceBindings.sha256===hashFile(boundFile),'MATRIX_GENERATOR_RECEIPT_DIFFERS');
 const copyNames=fs.readdirSync(generated).filter(n=>n.endsWith('.h')||n==='unified_binding.cpp');
 const names=[MATRIX_CHECKER,MATRIX_PROBE,'tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/fixtures/hc_remaining_model_capabilities/expectations.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'].map(n=>path.join(repo,n));
 const inputFiles=[...names,source,boundFile,path.join(generated,'motion_consumer.cpp'),path.join(generated,'expectations.json'),...copyNames.map(n=>ownedFile(generated,n))];
 const expectedInputs=pinnedMap(inputFiles.map(file=>({path:file,sha256:hashFile(file)}))),seen=new Set();
 need(report.inputs.length===expectedInputs.size,'MATRIX_INPUT_SET_DIFFERS');
 for(const row of report.inputs){need(row&&typeof row.file==='string'&&validHash(row.sha256)&&!seen.has(key(row.file))&&expectedInputs.get(key(row.file))?.sha256===row.sha256,'MATRIX_INPUT_SET_DIFFERS');seen.add(key(row.file));checkFile(row.file,row.sha256);if(!inside(path.resolve(generated),path.resolve(row.file)))need(allowed.get(key(row.file))?.sha256===row.sha256,'MATRIX_UNDECLARED_CHECKER_INPUT');}
 const ownedInputs=verifyMatrixProbeSources({repo,out,generated,report});
 const observedIncludes=[];
 for(const config of report.configurations){
  const profile=config.profile;checkFile(ownedFile(out,profile+'/probe.exe'),config.executableSha256);ownedFile(out,profile+'/probe.obj');if(profile==='Checked')ownedFile(out,profile+'/probe.pdb');ownedFile(out,profile+'-compile.log');
  const batch=fs.readFileSync(ownedFile(out,profile+'/compile.cmd'),'utf8');need(batch.includes(profile==='Release'?'/O2 /DNDEBUG /MD':'/Od /RTC1 /Zi /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2 /MDd')&&batch.includes('/sourceDependencies')&&batch.includes('/std:c++20')&&batch.replaceAll('\\','/').includes(path.join(out,'source/probe.cpp').replaceAll('\\','/')),'MATRIX_COMPILE_COMMAND_INVALID');
  const log=readJson(ownedFile(out,profile+'-run.log'));need(same(log,config.result),'MATRIX_NATIVE_LOG_DIFFERS');
  verifyMatrixChildReceipt({out,generated,config,role:'compile',checkerPid});
  verifyMatrixChildReceipt({out,generated,config,role:'native',checkerPid});
  const dependency=readJson(ownedFile(out,profile+'/dependencies.json'));
  observedIncludes.push({profile,rows:verifyMatrixIncludes(config.actualIncludes,dependency,{sourceFile:path.join(out,'source/probe.cpp'),sourceInputs,toolInputs,ownedInputs})});
 }
 return {status:'PASS',validation,resultFile,report,observedIncludes,closure:ownedTree(out).map(file=>({path:file,sha256:hashFile(file)}))};
}
/** Read-only probe identity check; this alone grants no execution acceptance. */
export function verifyMatrixProbeSources({repo,out,generated,report}){
 const copyNames=fs.readdirSync(generated).filter(n=>n.endsWith('.h')||n==='unified_binding.cpp');
 const ownedInputs=[];
 const addOwned=(relative,expected)=>{const file=ownedFile(out,relative);need(hashFile(file)===sha(expected),'MATRIX_GENERATED_PROBE_DIFFERS');ownedInputs.push({path:file,sha256:hashFile(file)});};
 const expected=expectedProbe(generated,path.join(repo,MATRIX_PROBE));
 need(report.instrumentation.unusedFullSuiteEntryOmitted===true&&same(report.instrumentation.omittedEntry,expected.omittedEntry),'MATRIX_OMITTED_ENTRY_PROOF_DIFFERS');
 addOwned('source/probe.cpp',expected.code);
 for(const name of copyNames){const bytes=fs.readFileSync(ownedFile(generated,name));addOwned('source/'+name,name==='hc_model_motion_catalog.snapshot.h'?instrumentHeader(bytes.toString('utf8')):bytes);}
 const originalHeader=ownedFile(generated,'hc_model_motion_catalog.snapshot.h'),instrumentedHeader=ownedFile(out,'source/hc_model_motion_catalog.snapshot.h');
 const frozen=fs.readFileSync(path.join(repo,MATRIX_PROBE),'utf8').match(/Baseline function SHA256: ([a-f0-9]{64})/)?.[1];
 need(report.instrumentation.lookupHeaderOriginalSha256===hashFile(originalHeader)&&report.instrumentation.lookupHeaderInstrumentedSha256===hashFile(instrumentedHeader)&&report.instrumentation.immutableOriginalComparisonSha256===frozen,'MATRIX_INSTRUMENTATION_HASH_DIFFERS');
 return ownedInputs;
}
/** Natural process identity is checked separately from JSON assertion summaries. */
export function verifyMatrixChildReceipt({out,generated,config,role,checkerPid}){
 const compile=role==='compile',profile=config.profile,label=profile+'-'+(compile?'compile':'run'),ref=compile?config.compileProcess:config.nativeProcess;
 need(Number.isSafeInteger(checkerPid)&&checkerPid>0&&object(ref)&&typeof ref.file==='string'&&validHash(ref.sha256),'MATRIX_ACTUAL_PROCESS_RECEIPT_REQUIRED');
 const file=ownedFile(out,path.relative(out,ref.file));need(key(file)===key(path.join(out,label+'.process.json')),'MATRIX_PROCESS_RECEIPT_PATH');checkFile(file,ref.sha256);const receipt=readJson(file);
 need(receipt.schemaVersion===1&&receipt.kind==='matrix-actual-child'&&receipt.state==='TERMINAL'&&receipt.label===label&&receipt.parentPid===checkerPid&&Number.isSafeInteger(receipt.pid)&&receipt.pid>0&&receipt.pid!==checkerPid&&receipt.exitCode===(compile?0:config.executionExit)&&receipt.signal===null&&receipt.timedOut===false&&receipt.launchError===null&&receipt.outputTruncated===false&&receipt.timeoutMs===60000&&receipt.maxBuffer===8*1024*1024&&Number.isFinite(receipt.durationMs)&&receipt.durationMs>=0&&receipt.durationMs<=60000&&receipt.finishedAt-receipt.startedAt===receipt.durationMs,'MATRIX_ACTUAL_CHILD_EXIT_INVALID');
 const folder=path.join(out,profile),input=ownedFile(out,profile+'/'+(compile?'compile.cmd':'probe.exe'));
 need(key(receipt.cwd)===key(folder)&&receipt.input&&key(receipt.input.path)===key(input)&&receipt.input.beforeSha256===hashFile(input)&&receipt.input.afterSha256===hashFile(input)&&receipt.input.readError===undefined,'MATRIX_PROCESS_INPUT_CHANGED');
 need(compile?receipt.executable==='cmd.exe'&&Array.isArray(receipt.args)&&receipt.args.length===3&&receipt.args[0]==='/d'&&receipt.args[1]==='/c'&&key(receipt.args[2])===key(input):key(receipt.executable)===key(input)&&Array.isArray(receipt.args)&&receipt.args.length===1&&key(receipt.args[0])===key(path.join(generated,'expectations.json')),'MATRIX_ACTUAL_CHILD_COMMAND_DIFFERS');
 for(const stream of ['stdout','stderr']){const log=ownedFile(out,label+'.'+stream+'.log'),record=receipt.streams?.[stream];need(record&&key(record.file)===key(log)&&record.sha256===hashFile(log),'MATRIX_ACTUAL_CHILD_STREAM_DIFFERS');}
 if(compile)need(Array.isArray(receipt.phaseEvents)&&same(receipt.phaseEvents.map(row=>row.phase),['vcvars-start','vcvars-complete','compile-start','compile-complete'])&&receipt.phaseEvents.every((row,i,all)=>Number.isFinite(row.elapsedMs)&&row.elapsedMs>=0&&row.elapsedMs<=receipt.durationMs&&(i===0||row.elapsedMs>=all[i-1].elapsedMs)),'MATRIX_COMPILE_PHASES_REQUIRED');
 return {path:file,sha256:hashFile(file),pid:receipt.pid,exitCode:receipt.exitCode,durationMs:receipt.durationMs,phaseEvents:receipt.phaseEvents};
}
