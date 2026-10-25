import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {extract} from '../../../fixtures/legion_motion_consumer/extract_main.mjs';
import {closeBrace} from '../../../fixtures/hc_remaining_model_capabilities/expectations.mjs';
import {hashFile,readJson,ownedFile,ownedTree} from './evidence.mjs';
import {sha,verifyMatrixIncludes} from './matrix-evidence.mjs';
import {validCount,validHash} from './contracts.mjs';
export const OWNER_CHECKER='tools/hc_motion_primary_owner_selftest.mjs';
export const OWNER_CONTRACT='tools/fixtures/hc_model_motion_consumer/primary-owner-contract.json';
export const OWNER_PROBE='tools/fixtures/hc_model_motion_consumer/owner_binding_probe.cpp';
export const OWNER_TRACE='tools/ymcc_sim/global/motion-consumer-signals/owner-child-trace.mjs';
export const OWNER_BUDGET_MS=240000,OWNER_CHILD_TIMEOUT_MS=60000,OWNER_OUTPUT_LIMIT=2*1024*1024;
// Same five original D16 faults, assertions and dual native configurations.
export const OWNER_FAULTS=Object.freeze([
 Object.freeze({id:'trust-claimed-kind',part:'header',expected:'REVIEWED_CLASS_CANNOT_FALL_BACK_TO_LEGACY'}),
 Object.freeze({id:'skip-default-dmi',part:'main',expected:'LIVE_BOUND_EQUIVALENCE'}),
 Object.freeze({id:'wrong-reviewed-route',part:'main',expected:'LIVE_BOUND_EQUIVALENCE'}),
 Object.freeze({id:'rollback-class-search',part:'main',expected:'NO_REPEATED_CLASS_TABLE_SEARCH'}),
 Object.freeze({id:'wrong-primary-priority',part:'catalog',expected:'GENERATED_PRIMARY_OWNER_PRIORITY'}),
]);
const need=(value,code)=>{if(!value)throw Error(code);},key=file=>path.resolve(file).toLowerCase(),same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const object=value=>value!==null&&typeof value==='object'&&!Array.isArray(value);
export function assertOwnerProcess(call){need(call?.exitCode===0&&Number.isSafeInteger(call.pid)&&call.pid>0&&!call.timedOut&&!call.signal&&!call.launchError&&!call.outputTruncated,'OWNER_CHECKER_PROCESS_FAILED');}
export function ownerCheckerArguments({repo,out,source,generated,fault}){
 need(typeof generated==='string'&&(!fault||OWNER_FAULTS.some(x=>same(x,{id:fault.id,part:fault.part,expected:fault.expected}))),'OWNER_INVOCATION_INVALID');
 const args=['--import',pathToFileURL(path.join(repo,OWNER_TRACE)).href,path.join(repo,OWNER_CHECKER),'--repo',repo,'--reference',repo,'--source',source,'--generated',generated,'--out',out,'--profiles','Release,Checked'];
 if(fault)args.push('--expect-failure',fault.expected);return args;
}
/** Byte controls only. Neither these mutations nor schema tests prove execution. */
export function createOwnerFaults({sourceBytes,headerBytes,catalogBytes,contract}){
 const original={main:sourceBytes,header:headerBytes,catalog:catalogBytes};
 const text=Object.fromEntries(Object.entries(original).map(([part,bytes])=>{const value=bytes.toString('utf8');need(Buffer.from(value).equals(bytes),'OWNER_SOURCE_UTF8_REQUIRED');return[part,value];}));
 const unique=(body,from,to)=>{need(body.split(from).length===2,'OWNER_FAULT_HUNK_NOT_UNIQUE');return body.replace(from,to);};
 return OWNER_FAULTS.map(fault=>{
  let before=text[fault.part],after;
  if(fault.id==='trust-claimed-kind')after=unique(before,'row->primaryKind : Kind::None','binding.kind : Kind::None');
  if(fault.id==='wrong-reviewed-route')after=unique(before,'case ymcc::capabilities::motion::Kind::Reviewed: return inputCaptureNewHcMotionSelectorBound();','case ymcc::capabilities::motion::Kind::Reviewed: return inputCaptureLegacyHcMotionSelectorBound();');
  if(fault.id==='skip-default-dmi'||fault.id==='rollback-class-search'){
   const symbol=fault.id==='skip-default-dmi'?'inputCaptureDefaultHcMotionIdentity':'inputCaptureMotionSelectorBound',entry=extract(before,symbol),body=entry.code,newline=body.includes('\r\n')?'\r\n':'\n';
   let replacement;
   if(fault.id==='skip-default-dmi'){need(body.split('    const auto manufacturer=').length===2,'OWNER_DEFAULT_DMI_HUNK');replacement=body.slice(0,body.indexOf('    const auto manufacturer='))+'    return true;'+newline+'}';}
   else{const frozen=contract?.methods?.find(m=>m.symbol===symbol);need(frozen&&sha(frozen.before)===frozen.beforeSha256,'OWNER_ROLLBACK_PREIMAGE_CHANGED');replacement=frozen.before.replaceAll('\r\n','\n').replaceAll('\n',newline);}
   after=unique(before,body,replacement);
  }
  if(fault.id==='wrong-primary-priority'){const row=before.split('\n').find(x=>x.includes('{Kind::Legacy,')&&x.includes(', Kind::Reviewed}'));need(row,'OWNER_SUPERSEDED_METADATA_REQUIRED');after=unique(before,row,row.replace(', Kind::Reviewed}',', Kind::Legacy}'));}
  need(typeof after==='string'&&after!==before,'OWNER_FAULT_UNCHANGED');
  let start=0;while(start<before.length&&before[start]===after[start])++start;
  let end=0;while(end<before.length-start&&end<after.length-start&&before[before.length-1-end]===after[after.length-1-end])++end;
  const changed=after.slice(start,after.length-end),removed=before.slice(start,before.length-end);
  const reverse=after.slice(0,start)+removed+after.slice(after.length-end);need(reverse===before,'OWNER_FAULT_REVERSE_FAILED');
  return {...fault,bytes:{...original,[fault.part]:Buffer.from(after)},proof:{sourceBeforeSha256:sha(sourceBytes),partBeforeSha256:sha(original[fault.part]),partAfterSha256:sha(after),prefixSha256:sha(before.slice(0,start)),suffixSha256:sha(before.slice(before.length-end)),removedSha256:sha(removed),insertedSha256:sha(changed),reverseSha256:sha(reverse),singleChangedPart:fault.part}};
 });
}
export function validateOwnerReport(report,{fault,oracle}={}){
 const issues=[],add=code=>{if(!issues.includes(code))issues.push(code);};
 if(fault&&!OWNER_FAULTS.some(f=>f.id===fault.id&&f.part===fault.part&&f.expected===fault.expected))add('OWNER_UNKNOWN_FAULT');
 if(!object(report)||report.schemaVersion!==1||report.status!=='PASS'||report.expectedFailure!==(fault?.expected??null))add('OWNER_REPORT_INVALID');
 if(!Array.isArray(report?.configurations)||!same(report.configurations.map(c=>c?.mode),['Release','Checked']))add('OWNER_BOTH_CONFIGURATIONS_REQUIRED');
 for(const c of Array.isArray(report?.configurations)?report.configurations:[]){
  if(c?.compileExit!==0||c.exitCode!==(fault?1:0)||c.signal!==null||c.accepted!==true||!Array.isArray(c.dependencies)||!c.dependencies.length)add('OWNER_NATIVE_EXECUTION_INVALID');
  const r=c?.report;if(!object(r)||!validCount(r.checks)||!r.checks)add('OWNER_ASSERTIONS_REQUIRED');
  if(fault){
   const error=r?.error,expected=fault.expected,exact=error===expected||(expected==='NO_REPEATED_CLASS_TABLE_SEARCH'&&typeof error==='string'&&error.startsWith(expected+':'));
   if(r?.status!=='FAIL'||!exact||typeof r?.scenario!=='string'||!validCount(r.windows))add('OWNER_EXACT_SEMANTIC_FAULT_REQUIRED');
   if(expected==='NO_REPEATED_CLASS_TABLE_SEARCH'&&typeof error==='string')try{const cost=JSON.parse(error.slice(expected.length+1));if(!['reviewedFindCalls','defaultFindCalls','legacyContainsCalls'].some(n=>validCount(cost[n])&&cost[n]>0))add('OWNER_ROLLBACK_COST_REQUIRED');}catch{add('OWNER_ROLLBACK_COST_REQUIRED');}
  }else{
   const counts=['boundPairs','windows','admitted','rejected','oldProcessCalls','newProcessCalls','supersededLegacyAttempts'];
   if(r?.status!=='PASS'||r.error!==undefined||counts.some(n=>!validCount(r?.[n])||!r[n])||r.oldProcessCalls!==r.newProcessCalls||r.admitted!==r.newProcessCalls||r.admitted+r.rejected!==r.windows||r.windows!==r.boundPairs*16||r.checks!==95+r.boundPairs+r.windows*15+r.supersededLegacyAttempts+3||r.fullBusExecuted!==false||r.realDevicesAccessed!==false||r.wholeProcessCpuReduction!==null)add('OWNER_COMPLETE_NATIVE_GRID_REQUIRED');
   if(!Array.isArray(oracle?.identities)||!Array.isArray(oracle?.legacy?.cases)||r?.boundPairs!==(oracle.identities.length+oracle.legacy.cases.length+7)*35)add('OWNER_COMPLETE_ORACLE_GRID_REQUIRED');
   const costs=['reviewedFindCalls','reviewedFindComparisons','defaultFindCalls','defaultFindComparisons','legacyContainsCalls','legacyContainsComparisons'];
   if(!object(r?.oldCost)||!object(r?.newCost)||costs.some(n=>!validCount(r?.oldCost?.[n])||!r.oldCost[n]||r?.newCost?.[n]!==0)||!validCount(r?.newCost?.totalCatalogPrimaryReads)||!r.newCost.totalCatalogPrimaryReads||r?.oldCost?.totalCatalogPrimaryReads!==0)add('OWNER_REAL_LOOKUP_REDUCTION_REQUIRED');
  }
 }
 if(!Array.isArray(report?.inputs)||!report.inputs.length||report.inputs.length>128||!Array.isArray(report?.drift)||report.drift.length||report?.originalLoopAlgorithmsPreserved!==true||report?.productionBodiesRewritten!==false||report?.baselineHelperSymbolRenamedOnlyToAvoidADL!==true||report?.fullBusExecuted!==false||report?.hardwareIo!==false||report?.wholeProcessCpuReduction!==null)add('OWNER_SCOPE_OR_DRIFT_INVALID');
 if(!Array.isArray(report?.observations)||!same(report.observations.map(r=>r?.file),['hc_model_motion_catalog.snapshot.h','hc_default_motion_catalog.h','hc_legacy_motion_catalog.snapshot.h'])||report.observations.some(r=>r.algorithmPreserved!==true||!validHash(r.originalSha256)||!validHash(r.instrumentedSha256)||r.originalSha256===r.instrumentedSha256))add('OWNER_LOOKUP_INSTRUMENTATION_REQUIRED');
 return {status:issues.length?'FAIL':'PASS',structuralValidationOnly:true,issues};
}
function instrumentHeader(original,symbol,comparison){
 const start=original.indexOf(symbol);need(start>=0&&original.indexOf(symbol,start+1)<0,'OWNER_LOOKUP_DEFINITION');
 const open=original.indexOf('{',start),end=closeBrace(original,open),method=original.slice(start,end);need(method.split(comparison).length===2,'OWNER_LOOKUP_COMPARISON');
 const localOpen=method.indexOf('{'),changed=(method.slice(0,localOpen+1)+'\n    if(!std::is_constant_evaluated())++ownerProbeCalls;'+method.slice(localOpen+1)).replace(comparison,'ownerProbeEqual('+comparison.split(/\s*==\s*/).join(', ')+')');
 const observer='inline unsigned long long ownerProbeCalls=0,ownerProbeComparisons=0;\ninline constexpr bool ownerProbeEqual(std::string_view left,std::string_view right) noexcept {if(!std::is_constant_evaluated())++ownerProbeComparisons;return left==right;}\n';
 return (original.slice(0,start)+observer+original.slice(start).replace(method,changed)).replace('#include <string_view>','#include <string_view>\n#include <type_traits>');
}
export function verifyOwnerProbeSources({repo,out,generated,report}){
 const sourceFile=ownedFile(generated,'motion_consumer.cpp');let code=fs.readFileSync(sourceFile,'utf8');
 const ns=code.indexOf('namespace candidate {');need(ns>=0,'OWNER_CANDIDATE_NAMESPACE');const body=code.slice(ns,closeBrace(code,code.indexOf('{',ns))+1),matches=[...body.matchAll(/^static ProbeMotionResult probeActive[^\n]*\) \{/gm)];need(matches.length===1,'OWNER_ACTIVE_PREFIX');
 const start=matches[0].index,active=body.slice(start,closeBrace(body,start+matches[0][0].lastIndexOf('{'))+1),contract=readJson(path.join(repo,OWNER_CONTRACT));
 need(contract.schemaVersion===1&&contract.methods.length===3&&contract.methods.every(m=>sha(m.before)===m.beforeSha256),'OWNER_BASELINE_CONTRACT_INVALID');
 let proof=fs.readFileSync(path.join(repo,OWNER_PROBE),'utf8');for(const marker of ['@@BEFORE_METHODS@@','@@BASELINE_ACTIVE_PROBE@@'])need(proof.split(marker).length===2,'OWNER_PROBE_MARKER');
 proof=proof.replace('@@BEFORE_METHODS@@',contract.methods.map(x=>x.before).join('\n\n')).replace('@@BASELINE_ACTIVE_PROBE@@',active);need(code.split('int main(int argc,char** argv)').length===2,'OWNER_PROBE_ENTRY');code=code.replace('int main(int argc,char** argv)','int UnusedFullMotionMain(int argc,char** argv)')+'\n'+proof;
 const owned=[],add=(name,bytes)=>{const file=ownedFile(out,'source/'+name);need(hashFile(file)===sha(bytes),'OWNER_GENERATED_PROBE_DIFFERS');owned.push({path:file,sha256:hashFile(file)});};add('probe.cpp',code);
 const instrumentation={
  'hc_model_motion_catalog.snapshot.h':['inline constexpr const Recipe* find(','recipe.deviceClass == deviceClass'],
  'hc_default_motion_catalog.h':['inline constexpr const hc_model_motion::Recipe* find(','row.deviceClass==deviceClass'],
  'hc_legacy_motion_catalog.snapshot.h':['inline constexpr bool containsClass(','recipe.deviceClass == deviceClass'],
 };
 for(const name of fs.readdirSync(generated).filter(n=>n.endsWith('.h')||n==='unified_binding.cpp')){
  const bytes=fs.readFileSync(ownedFile(generated,name));let value=bytes;
  if(instrumentation[name]){value=instrumentHeader(bytes.toString('utf8'),...instrumentation[name]);const observed=report.observations.find(r=>r.file===name);need(observed?.originalSha256===sha(bytes)&&observed.instrumentedSha256===sha(value),'OWNER_INSTRUMENTATION_HASH_DIFFERS');}
  if(name==='HcMotionCapabilityCatalog.h'){const text=bytes.toString('utf8');need(text.split('inline Kind primaryKind(').length===2,'OWNER_PRIMARY_KIND_DEFINITION');value=text.replace('inline Kind primaryKind(', 'inline unsigned long long ownerProbeReads=0;\ninline Kind primaryKind(').replace('    const auto* row=descriptor(binding);','    ++ownerProbeReads;\n    const auto* row=descriptor(binding);');}
  add(name,value);
 }
 return owned;
}
const checkFile=(file,pin)=>{need(validHash(pin)&&hashFile(file)===pin,'OWNER_FILE_HASH_MISMATCH');};
/** Match actual original checker files, exact includes and traced native exits. */
export function verifyOwnerEvidence({repo,out,generated,source,sourceSha256,sourceInputs,externalInputs,toolInputs,fault,oracleSha256,checkerPid}){
 const resultFile=ownedFile(out,'result.json'),report=readJson(resultFile),oracle=readJson(ownedFile(generated,'expectations.json'));
 const validation=validateOwnerReport(report,{fault,oracle});need(validation.status==='PASS','OWNER_STRUCTURED_REPORT_REJECTED');
 checkFile(source,sourceSha256);const boundFile=ownedFile(generated,'source-bindings.json'),bound=readJson(boundFile);
 need(key(bound.out??'')===key(generated)&&key(bound.source?.path??'')===key(source)&&bound.source?.sha256===sourceSha256&&bound.mode==='candidate'&&['pass','generated-only'].includes(bound.status),'OWNER_GENERATED_SOURCE_IDENTITY');
 checkFile(ownedFile(generated,'motion_consumer.cpp'),bound.generatedCppSha256);checkFile(ownedFile(generated,'expectations.json'),bound.oracleSha256);need(validHash(oracleSha256)&&bound.oracleSha256===oracleSha256,'OWNER_ORACLE_DIFFERS_FROM_ORIGINAL');
 const pins=new Map();for(const row of [...sourceInputs,...externalInputs,...toolInputs,{path:source,sha256:sourceSha256}]){need(row&&typeof row.path==='string'&&path.isAbsolute(row.path)&&validHash(row.sha256),'OWNER_PIN_INVALID');const prev=pins.get(key(row.path));need(!prev||prev===row.sha256,'OWNER_PIN_CONFLICT');pins.set(key(row.path),row.sha256);}
 for(const row of ['source','before','historical','capabilityBefore','legacyCatalog','legacyOracle','catalog','msi','runner'].map(n=>bound[n]).concat(bound.fixtures??[])){need(row&&typeof row.path==='string'&&pins.get(key(row.path))===row.sha256,'OWNER_UNRECORDED_GENERATOR_INPUT');checkFile(row.path,row.sha256);}
 const expectedFiles=[path.resolve(source),path.resolve(repo)+'/'+OWNER_PROBE,path.resolve(repo)+'/'+OWNER_CONTRACT,path.resolve(repo,OWNER_CHECKER),path.resolve(generated)+'/source-bindings.json',path.resolve(generated)+'/motion_consumer.cpp',...bound.fixtures.map(r=>r.path)];
 // Preserve the checker's actual string-level deduplication: a fixture can
 // occur twice via slash variants. It is not an invented extra input.
 const expected=[...new Set(expectedFiles)].map(file=>({file,sha256:hashFile(file)}));need(report.inputs.length===expected.length,'OWNER_INPUT_SET_DIFFERS');
 for(let i=0;i<expected.length;i++){const row=report.inputs[i],pin=expected[i];need(row&&typeof row.file==='string'&&key(row.file)===key(pin.file)&&row.sha256===pin.sha256,'OWNER_INPUT_SET_DIFFERS');checkFile(row.file,row.sha256);}
 const ownedInputs=verifyOwnerProbeSources({repo,out,generated,report}),observedIncludes=[],children=[];
 need(Number.isSafeInteger(checkerPid)&&checkerPid>0,'OWNER_CHECKER_PID_REQUIRED');
 const processRoot=path.join(out,'child-processes');need(fs.readdirSync(processRoot).sort().join(',')==='0.json,1.json,2.json,3.json','OWNER_ALL_CHILD_RECEIPTS_REQUIRED');
 const pids=new Set([checkerPid]);
 for(let i=0;i<report.configurations.length;i++){
  const c=report.configurations[i],mode=c.mode,exe=ownedFile(out,mode+'/probe.exe'),batch=ownedFile(out,mode+'/compile.cmd');ownedFile(out,mode+'/probe.obj');if(mode==='Checked')ownedFile(out,mode+'/probe.pdb');
  const command=fs.readFileSync(batch,'utf8');need(command.includes(mode==='Release'?'/O2 /DNDEBUG /MD':'/Od /RTC1 /Zi /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2 /MDd')&&command.includes('/sourceDependencies')&&command.includes('/std:c++20')&&command.replaceAll('\\','/').includes(path.join(out,'source/probe.cpp').replaceAll('\\','/')),'OWNER_COMPILE_COMMAND_INVALID');
  const nativeLog=readJson(ownedFile(out,mode+'-run.stdout.log'));need(same(nativeLog,c.report),'OWNER_NATIVE_LOG_DIFFERS');
  for(let j=0;j<2;j++){
   const n=2*i+j,compiler=j===0,r=readJson(ownedFile(out,'child-processes/'+n+'.json')),input=compiler?batch:exe;
   need(r.schemaVersion===1&&r.sequence===n&&r.mode===mode&&r.kind===(compiler?'owner-compile-child':'owner-native-child')&&r.parentPid===checkerPid&&Number.isSafeInteger(r.pid)&&r.pid>0&&!pids.has(r.pid)&&r.exitCode===(compiler?0:fault?1:0)&&r.signal===null&&r.error===null&&r.timedOut===false&&r.outputTruncated===false&&r.timeoutMs===60000&&r.maxBuffer===8*1024*1024&&Number.isSafeInteger(r.startedAt)&&Number.isSafeInteger(r.finishedAt)&&r.finishedAt>=r.startedAt&&r.finishedAt-r.startedAt<=60000&&key(r.cwd)===key(path.join(out,mode)),'OWNER_ACTUAL_CHILD_EXECUTION_INVALID');pids.add(r.pid);
   // The unchanged checker uses forward slashes after each absolute root.
   need(compiler?r.executable==='cmd.exe'&&r.args.length===3&&r.args[0]==='/d'&&r.args[1]==='/c'&&key(r.args[2])===key(batch):key(r.executable)===key(exe)&&r.args.length===1&&key(r.args[0])===key(path.join(generated,'expectations.json')),'OWNER_CHILD_COMMAND_DIFFERS');
   need(r.input&&key(r.input.path)===key(input)&&r.input.beforeSha256===hashFile(input)&&r.input.afterSha256===hashFile(input),'OWNER_CHILD_BINARY_OR_COMMAND_CHANGED');
   for(const stream of ['stdout','stderr'])need(r.streams?.[stream]===hashFile(ownedFile(out,mode+'-'+(compiler?'compile':'run')+'.'+stream+'.log')),'OWNER_CHILD_STREAM_DIFFERS');
   children.push({path:path.join(processRoot,n+'.json'),sha256:hashFile(path.join(processRoot,n+'.json')),pid:r.pid,kind:r.kind,exitCode:r.exitCode});
  }
  const dependency=readJson(ownedFile(out,mode+'/dependencies.json'));
  observedIncludes.push({mode,rows:verifyMatrixIncludes(c.dependencies,dependency,{sourceFile:path.join(out,'source/probe.cpp'),sourceInputs,toolInputs,ownedInputs}),executableSha256:hashFile(exe)});
 }
 return {status:'PASS',validation,resultFile,report,children,observedIncludes,closure:ownedTree(out).map(file=>({path:file,sha256:hashFile(file)}))};
}
export function validateOwnerStage(stage){
 const faults=stage?.faults;
 const valid=stage?.status==='PASS'&&stage.positive?.status==='PASS'&&stage.positive.configurations?.length===2&&same(stage.positive.configurations.map(c=>c.mode),['Release','Checked'])&&stage.positive.configurations.every(c=>c.compileExit===0&&c.exitCode===0&&c.accepted===true&&c.report?.status==='PASS')&&Array.isArray(faults)&&faults.length===5&&new Set(faults.map(f=>f.id)).size===5&&OWNER_FAULTS.every(f=>faults.some(r=>r.id===f.id&&r.part===f.part&&r.expected===f.expected&&r.status==='PASS'&&r.configurations?.length===2&&same(r.configurations.map(c=>c.mode),['Release','Checked'])&&r.configurations.every(c=>c.compileExit===0&&c.exitCode===1&&c.accepted===true)))&&stage.semanticSourceFaultsDetected===4&&stage.logicalLookupRegressionDetected===1;
 need(valid,'OWNER_ALL_FIVE_DUAL_CONFIG_FAULTS_REQUIRED');return {status:'PASS',positiveConfigurations:2,faultGroups:5,faultConfigurations:10};
}
