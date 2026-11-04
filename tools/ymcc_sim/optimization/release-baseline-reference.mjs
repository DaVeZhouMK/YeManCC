// The release alias consumes the parent's single fresh original baseline; no producer is launched here.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
import {performance} from 'node:perf_hooks';
import {parentDeclaration,validateBaselineReceipt,sourceMap,verifyParentMetadata,PARENT_ID} from '../global/decky-persistent-lifecycle/current-parent-global.mjs';
import {baselineEnvironment,packageMaintenanceEnvironment,resourceFixtureEnvironment} from '../global/decky-persistent-lifecycle/baseline-environment.mjs';
import {verifyNativeResourceReference,verifyNativeHealthReference,verifyNativeRuntimeReference} from './release-resource-reference.mjs';
import {verifyPackageFixtureTools} from '../global/decky-update-recovery/package-fixture-tools.mjs';
import {simulationCorePath} from '../global/release-host-environment.mjs';
export const REVIEWED_RELEASE_ALIAS_SHA256='f80bd09d210eff41cd845d82f2f97105ea12b578d73f575c1252c708ce69ed80';
const ROOT=path.resolve(import.meta.dirname,'../../..');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const key=file=>path.resolve(file).toLowerCase();
const inside=(root,file)=>{const r=path.relative(root,file);return r!==''&&!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep);};
const pin=file=>({path:path.resolve(file),sha256:hash(file),bytes:fs.statSync(file).size});
function verifyPin(row){assert(row&&path.isAbsolute(row.path)&&/^[0-9a-f]{64}$/.test(row.sha256),'BASELINE_REFERENCE_PIN_REQUIRED');assert.equal(hash(row.path),row.sha256,'BASELINE_REFERENCE_PIN_CHANGED');if(row.bytes!==undefined)assert.equal(fs.statSync(row.path).size,row.bytes,'BASELINE_REFERENCE_SIZE_CHANGED');return row;}
function mapTools(rows){const m=new Map();for(const row of rows){assert(!m.has(key(row.path)),'BASELINE_REFERENCE_DUPLICATE_TOOL');m.set(key(row.path),row.sha256);}return m;}

// These are the five release requirements, not the parent's larger JS regression list.
export const RELEASE_JAVASCRIPT_STEPS=Object.freeze([
 'decky_loader_official_selftest','decky_sidebar_safety_selftest','decky_sidebar_client_selftest',
 'decky_sidebar_host_selftest','decky_sidebar_stability_matrix_selftest'
]);
export const RELEASE_JAVASCRIPT_ENVIRONMENT_KEYS=Object.freeze([
 'SystemRoot','ComSpec','PATH','PSModulePath','NODE_OPTIONS','NODE_PATH','ESBUILD_BINARY_PATH',
 'ESBUILD_WORKER_THREADS','YMCC_MIRROR_CLIENT_SOURCE_OVERRIDE','YMCC_FAN_MIRROR_HOST_SOURCE_OVERRIDE',
 'YMCC_DECKY_ARTIFACT_ROOT','YEMAN_WORKSPACE_ROOT','YMCC_SIM_RAW_PROGRAM_PWSH'
]);
export const releaseJavascriptNames=()=>[...RELEASE_JAVASCRIPT_STEPS];
export const releaseJavascriptEnvironmentKeys=()=>[...RELEASE_JAVASCRIPT_ENVIRONMENT_KEYS];
const JS_REPORTS={
 decky_loader_official_selftest:'OFFICIAL-LOADER-CONSUMER.json',
 decky_sidebar_safety_selftest:'SAFETY-NOTIFICATION.json',
 decky_sidebar_client_selftest:'validation/MIRROR-CLIENT-SELFTEST.json',
 decky_sidebar_stability_matrix_selftest:'validation/DECKY-STABILITY-MATRIX.json'
};
const JS_OVERRIDES=['YMCC_MIRROR_CLIENT_SOURCE_OVERRIDE','YMCC_FAN_MIRROR_HOST_SOURCE_OVERRIDE','ESBUILD_BINARY_PATH'];
function javascriptEnvironment(value){
 assert(value&&typeof value==='object'&&!Array.isArray(value),'JAVASCRIPT_REFERENCE_ENVIRONMENT_REQUIRED');
 assert.deepEqual(Object.keys(value).sort(),[...RELEASE_JAVASCRIPT_ENVIRONMENT_KEYS].sort(),'JAVASCRIPT_REFERENCE_ENVIRONMENT_KEYS');
 for(const name of RELEASE_JAVASCRIPT_ENVIRONMENT_KEYS)assert(value[name]===null||typeof value[name]==='string','JAVASCRIPT_REFERENCE_ENVIRONMENT_VALUE:'+name);
 // A finite observation cannot establish arbitrary preload/module-resolution closures.
 for(const name of ['NODE_OPTIONS','NODE_PATH'])assert(value[name]===null||value[name]==='','JAVASCRIPT_REFERENCE_UNKNOWN_NODE_CLOSURE:'+name);
 return value;
}
function javascriptCases(name,value){
 assert(value&&typeof value==='object'&&!Array.isArray(value),'JAVASCRIPT_REFERENCE_RAW_SCHEMA');
 const host=name==='decky_sidebar_host_selftest',safety=name==='decky_sidebar_safety_selftest';
 if(safety)assert.equal(value.passed,true,'JAVASCRIPT_REFERENCE_RAW_NOT_PASS');
 else if(name==='decky_sidebar_client_selftest')assert.equal(value.productionClientSource,true,'JAVASCRIPT_REFERENCE_RAW_NOT_PASS');
 else assert.equal(value.status,'passed','JAVASCRIPT_REFERENCE_RAW_NOT_PASS');
 if('status' in value)assert.equal(value.status,'passed','JAVASCRIPT_REFERENCE_RAW_NOT_PASS');
 if('passed' in value&&typeof value.passed==='boolean')assert.equal(value.passed,true,'JAVASCRIPT_REFERENCE_RAW_NOT_PASS');
 const cases=host?value.cases:value.results;
 assert(Array.isArray(cases)&&cases.length>0,'JAVASCRIPT_REFERENCE_CASES_REQUIRED');
 for(const row of cases){
  if(host||safety)assert(typeof row==='string'&&row.trim()!=='','JAVASCRIPT_REFERENCE_STRING_CASE_REQUIRED');
  else{
   assert(row&&typeof row==='object'&&!Array.isArray(row)&&typeof row.name==='string'&&row.name.trim()!=='','JAVASCRIPT_REFERENCE_CASE_SCHEMA');
   if(name==='decky_loader_official_selftest')assert.equal(row.passed,true,'JAVASCRIPT_REFERENCE_CASE_NOT_PASS');
   else assert.equal(row.status,'passed','JAVASCRIPT_REFERENCE_CASE_NOT_PASS');
   assert(row.passed!==false&&row.pass!==false&&row.executed!==false&&row.cached!==true&&!row.error,'JAVASCRIPT_REFERENCE_CASE_NOT_PASS');
  }
 }
 if(!host)assert.equal(value.cases,cases.length,'JAVASCRIPT_REFERENCE_CASE_COUNT');
 if(name==='decky_sidebar_stability_matrix_selftest'){
  assert.equal(value.failed,0,'JAVASCRIPT_REFERENCE_CASE_NOT_PASS');
  assert.equal(value.passed,cases.length,'JAVASCRIPT_REFERENCE_CASE_COUNT');
 }
 return cases;
}
// Only called after the entire existing parent/global/source/tool/metadata proof succeeds.
// This consumes original receipts; it never executes a producer or grants actual runtime PASS.
function verifyJavascriptReferences({repo,core,out,parent,global,declaration,sources,tools,request}){
 assert(request&&typeof request==='object'&&!Array.isArray(request),'JAVASCRIPT_REFERENCE_REQUEST_REQUIRED');
 assert.deepEqual(Object.keys(request).sort(),['schemaVersion','kind','program','cwd','environment','steps'].sort(),'JAVASCRIPT_REFERENCE_REQUEST_SCHEMA');
 assert.equal(request.schemaVersion,1,'JAVASCRIPT_REFERENCE_REQUEST_SCHEMA');
 assert.equal(request.kind,'ymcc-release-javascript-reference-request','JAVASCRIPT_REFERENCE_REQUEST_KIND');
 assert.deepEqual(request.steps,RELEASE_JAVASCRIPT_STEPS,'JAVASCRIPT_REFERENCE_STEPS');
 assert(typeof request.cwd==='string'&&path.isAbsolute(request.cwd)&&key(request.cwd)===key(repo),'JAVASCRIPT_REFERENCE_REQUEST_CWD');
 const requestedEnvironment=javascriptEnvironment(request.environment);
 const verified=new Map();
 const exactPin=(row,expectedFile)=>{
  assert(row&&typeof row.path==='string'&&path.isAbsolute(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256??'')&&(!Object.hasOwn(row,'bytes')||(Number.isSafeInteger(row.bytes)&&row.bytes>=0)),'JAVASCRIPT_REFERENCE_PIN_REQUIRED');
  if(expectedFile)assert.equal(key(row.path),key(expectedFile),'JAVASCRIPT_REFERENCE_FOREIGN_FILE');
  assert.equal(key(fs.realpathSync(row.path)),key(row.path),'JAVASCRIPT_REFERENCE_REPARSE');
  const stat=fs.statSync(row.path);assert(stat.isFile(),'JAVASCRIPT_REFERENCE_FILE_REQUIRED');
  // Parent pins contain path/SHA only. Optional supplied size is still a strict claim;
  // normalize both shapes using the actual file size before comparing request pins.
  assert(!Object.hasOwn(row,'bytes')||row.bytes===stat.size,'JAVASCRIPT_REFERENCE_SIZE_CHANGED');
  const actual={sha256:row.sha256,bytes:stat.size},previous=verified.get(key(row.path));
  if(previous)assert.deepEqual(actual,previous,'JAVASCRIPT_REFERENCE_PIN_CONFLICT');
  else{verifyPin(row);verified.set(key(row.path),actual);}
  return {path:path.resolve(row.path),...actual};
 };
 assert.deepEqual(Object.keys(request.program??{}).sort(),['path','sha256','bytes'].sort(),'JAVASCRIPT_REFERENCE_REQUEST_PROGRAM');
 assert(Number.isSafeInteger(request.program.bytes)&&request.program.bytes>=0,'JAVASCRIPT_REFERENCE_REQUEST_PROGRAM_BYTES');
 const requestedProgram=exactPin(request.program,processExec);
 assert.equal(tools.get(key(requestedProgram.path)),requestedProgram.sha256,'JAVASCRIPT_REFERENCE_NODE_NOT_BOUND');
 // parentDeclaration captures and verifies the full current bundled tool graph. Bind that
 // complete graph to the accepted parent tools as well as the resolved esbuild entry/backend.
 const authority=declaration.authority;
 assert(authority?.schemaVersion===1&&typeof authority.repoRoot==='string'&&key(authority.repoRoot)===key(repo),'JAVASCRIPT_REFERENCE_TOOL_AUTHORITY_REQUIRED');
 assert(Array.isArray(authority.tools)&&authority.tools.length>0,'JAVASCRIPT_REFERENCE_TOOL_CLOSURE_REQUIRED');
 const authorityTools=mapTools(authority.tools);
 for(const row of authority.tools){verifyPin(row);assert.equal(tools.get(key(row.path)),row.sha256,'JAVASCRIPT_REFERENCE_TOOL_NOT_BOUND');}
 for(const file of [authority.compilerEntry,authority.compilerBinary]){
  assert(typeof file==='string'&&path.isAbsolute(file)&&authorityTools.has(key(file))&&authorityTools.get(key(file))===tools.get(key(file)),'JAVASCRIPT_REFERENCE_ESBUILD_NOT_BOUND');
 }
 assert(typeof parent.work==='string'&&path.isAbsolute(parent.work)&&inside(path.join(core,'Workspace'),parent.work),'JAVASCRIPT_REFERENCE_WORK_OUTSIDE_CORE');
 assert.equal(key(fs.realpathSync(parent.work)),key(parent.work),'JAVASCRIPT_REFERENCE_WORK_REPARSE');
 const start=Date.parse(parent.startedAt),finish=Date.parse(parent.finishedAt),globalFinish=Date.parse(global.finishedAt);
 assert(Number.isFinite(globalFinish)&&finish<=globalFinish,'JAVASCRIPT_REFERENCE_GLOBAL_TIME');
 assert(global.scope!=='current-run-completed-parent-subset','JAVASCRIPT_REFERENCE_PARTIAL_GLOBAL');
 assert(Array.isArray(parent.cases)&&parent.cases.length>0&&parent.cases.every(row=>row.passed===true),'JAVASCRIPT_REFERENCE_PARENT_CASES_NOT_PASS');
 const processMetadata=read(path.join(out,'RAW-EVIDENCE.json')).processes;
 assert(Array.isArray(processMetadata),'JAVASCRIPT_REFERENCE_PROCESS_METADATA_REQUIRED');
 const currentFile=file=>{
  const mtime=Math.floor(fs.statSync(file).mtimeMs);
  assert(start<=mtime&&mtime<=finish,'JAVASCRIPT_REFERENCE_FILE_NOT_CURRENT');
 };
 // Only these two reviewed output roles may differ. All other values retain null/empty
 // distinctions and must match byte for byte. The fallback is inactive with explicit output.
 const outerArtifact=requestedEnvironment.YMCC_DECKY_ARTIFACT_ROOT;
 assert(typeof outerArtifact==='string'&&path.isAbsolute(outerArtifact),'JAVASCRIPT_REFERENCE_REQUEST_ARTIFACT_ROOT');
 assert.equal(key(outerArtifact),key(path.join(path.resolve(outerArtifact,'../../../../'),'Build/Validation/DeckyUpdate/scenarios')),'JAVASCRIPT_REFERENCE_REQUEST_ARTIFACT_ROLE');
 const outputFallback=value=>assert(value===null||value===''||(typeof value==='string'&&path.isAbsolute(value)),'JAVASCRIPT_REFERENCE_OUTPUT_FALLBACK_ROLE');
 outputFallback(requestedEnvironment.YEMAN_WORKSPACE_ROOT);
 return RELEASE_JAVASCRIPT_STEPS.map(name=>{
  const id='original.'+name,entry=path.join(repo,'tools',name+'.mjs'),artifact=path.join(parent.work,name);
  const matches=parent.processes.filter(row=>row.id===id);
  assert.equal(matches.length,1,'JAVASCRIPT_REFERENCE_PROCESS_NOT_UNIQUE:'+name);
  const process=matches[0],processFile=path.join(out,'processes',id+'.json');
  exactPin(pin(processFile),processFile);currentFile(processFile);
  assert.deepEqual(read(processFile),process,'JAVASCRIPT_REFERENCE_PROCESS_FILE_MISMATCH');
  const metadataRows=processMetadata.filter(row=>key(row.path)===key(processFile));
  assert.equal(metadataRows.length,1,'JAVASCRIPT_REFERENCE_PROCESS_METADATA_NOT_UNIQUE');
  assert.deepEqual(exactPin(metadataRows[0],processFile),pin(processFile),'JAVASCRIPT_REFERENCE_PROCESS_METADATA_BINDING');
  assert(Number.isSafeInteger(process.pid)&&process.pid>0,'JAVASCRIPT_REFERENCE_RUNTIME_PID_REQUIRED');
  const begin=Date.parse(process.startedAt),end=Date.parse(process.finishedAt);
  assert(Number.isFinite(begin)&&Number.isFinite(end)&&start<=begin&&begin<=end&&end<=finish,'JAVASCRIPT_REFERENCE_PROCESS_NOT_CURRENT');
  for(const [field,value] of Object.entries({exitCode:0,signal:null,error:null,timedOut:false,outputTruncated:false,originalBudgetMs:60000}))assert.equal(process[field],value,'JAVASCRIPT_REFERENCE_PROCESS_'+field);
  assert(Number.isFinite(process.elapsedMs)&&process.elapsedMs>=0&&Number.isFinite(process.timeoutMs)&&process.timeoutMs>0&&process.timeoutMs<=60000,'JAVASCRIPT_REFERENCE_PROCESS_TIMING');
  assert.deepEqual(process.arguments,[entry,artifact],'JAVASCRIPT_REFERENCE_ORIGINAL_ARGUMENTS');
  assert.equal(key(process.cwd),key(repo),'JAVASCRIPT_REFERENCE_CWD');
  assert.deepEqual(exactPin(process.program,requestedProgram.path),requestedProgram,'JAVASCRIPT_REFERENCE_NODE_CHANGED');
  for(const [field,suffix] of [['stdout','stdout.bin'],['stderr','stderr.bin']]){
   exactPin(process[field],path.join(out,'processes',id+'.'+suffix));currentFile(process[field].path);
  }
  const observation=process.javascriptInputs;
  assert(observation?.observedStable===true,'JAVASCRIPT_REFERENCE_OBSERVATION_UNSTABLE');
  assert.deepEqual(observation.before,observation.after,'JAVASCRIPT_REFERENCE_OBSERVATION_CHANGED');
  const before=observation.before;
  assert.equal(before?.schemaVersion,1,'JAVASCRIPT_REFERENCE_OBSERVATION_REQUIRED');
  const environment=javascriptEnvironment(before.environment);
  assert.equal(before.nodeOptionsPreloadClosureEstablished,true,'JAVASCRIPT_REFERENCE_NODE_OPTIONS_CLOSURE');
  assert.equal(before.nodePathResolutionClosureEstablished,true,'JAVASCRIPT_REFERENCE_NODE_PATH_CLOSURE');
  assert.deepEqual(exactPin(before.program,requestedProgram.path),requestedProgram,'JAVASCRIPT_REFERENCE_NODE_CHANGED');
  const observedEntry=exactPin(before.entry,entry);
  assert.equal(sources.get('tools/'+name+'.mjs'),observedEntry.sha256,'JAVASCRIPT_REFERENCE_ENTRY_NOT_BOUND');
  for(const envKey of RELEASE_JAVASCRIPT_ENVIRONMENT_KEYS){
   if(envKey==='YMCC_DECKY_ARTIFACT_ROOT'){
    assert(typeof environment[envKey]==='string'&&path.isAbsolute(environment[envKey])&&key(environment[envKey])===key(artifact),'JAVASCRIPT_REFERENCE_ARTIFACT_ROOT');
   }else if(envKey==='YEMAN_WORKSPACE_ROOT')outputFallback(environment[envKey]);
   else assert.equal(environment[envKey],requestedEnvironment[envKey],'JAVASCRIPT_REFERENCE_ENVIRONMENT_CHANGED:'+envKey);
  }
  const expectedOverrides=JS_OVERRIDES.filter(envKey=>environment[envKey]!==null&&environment[envKey]!=='');
  assert(Array.isArray(before.overrides),'JAVASCRIPT_REFERENCE_OVERRIDES_REQUIRED');
  assert.deepEqual(before.overrides.map(row=>row.name).sort(),[...expectedOverrides].sort(),'JAVASCRIPT_REFERENCE_OVERRIDES_CHANGED');
  for(const override of before.overrides){
   const file=path.resolve(repo,environment[override.name]),actual=exactPin(override,file);
   const relative=path.relative(repo,file).replaceAll('\\','/');
   assert(sources.get(relative)===actual.sha256||tools.get(key(file))===actual.sha256,'JAVASCRIPT_REFERENCE_OVERRIDE_NOT_BOUND');
   if(override.name==='ESBUILD_BINARY_PATH')assert.equal(key(file),key(authority.compilerBinary),'JAVASCRIPT_REFERENCE_ESBUILD_OVERRIDE');
  }
  assert.equal(key(fs.realpathSync(createRequire(entry).resolve('esbuild'))),key(authority.compilerEntry),'JAVASCRIPT_REFERENCE_ESBUILD_RESOLUTION');
  const host=name==='decky_sidebar_host_selftest',rawFile=host?process.stdout.path:path.join(artifact,JS_REPORTS[name]);
  const raw=exactPin(host?process.stdout:pin(rawFile),rawFile);currentFile(rawFile);
  if(!host){
   const rows=parent.rawEvidence.filter(row=>key(row.path)===key(rawFile));
   assert.equal(rows.length,1,'JAVASCRIPT_REFERENCE_RAW_NOT_UNIQUE');
   assert.deepEqual(exactPin(rows[0],rawFile),raw,'JAVASCRIPT_REFERENCE_RAW_NOT_IN_PARENT');
  }
  const cases=javascriptCases(name,read(rawFile)),caseRows=parent.cases.filter(row=>row.id===id);
  assert.equal(caseRows.length,1,'JAVASCRIPT_REFERENCE_PARENT_CASE_NOT_UNIQUE');
  assert.equal(caseRows[0].passed,true,'JAVASCRIPT_REFERENCE_PARENT_CASE_NOT_PASS');
  assert.equal(caseRows[0].originalCases,cases.length,'JAVASCRIPT_REFERENCE_PARENT_CASE_COUNT');
  assert.deepEqual(exactPin(caseRows[0].process,processFile),pin(processFile),'JAVASCRIPT_REFERENCE_PARENT_PROCESS_BINDING');
  return {requiredReleaseStep:name,name,status:'SHARED_REFERENCE',originalProducerExecutionsWithinParent:1,
   originalProducerExecutionsByThisReference:0,cached:false,actualPassedThisRun:false,executedThisRun:false,
   originalProcess:pin(processFile),originalResult:raw,originalProgram:process.program,
   originalElapsedMs:process.elapsedMs,originalArguments:process.arguments,originalCases:cases.length,
   originalCaseResults:cases,originalEnvironment:environment,verificationExecuted:true};
 });
}

// Called only after the enclosing current parent/global proof has been accepted.
// This checks the original complete nested package producer, never launches it.
export function verifyPackageMaintenanceReference({repoRoot,baselineResult,baselineValue,originalProcess,sourceInputs,toolArtifacts,expectedEnvironment=packageMaintenanceEnvironment(),authority=verifyPackageFixtureTools()}={}){
 const repo=fs.realpathSync(repoRoot),baselineFile=fs.realpathSync(baselineResult),base=path.dirname(baselineFile),binding=baselineValue.packageMaintenanceExecution;
 assert.equal(binding?.schemaVersion,1,'PACKAGE_REFERENCE_EXECUTION_BINDING_REQUIRED');
 assert.equal(baselineValue.passed,true,'PACKAGE_REFERENCE_BASELINE_NOT_PASS');
 const names={process:'package-maintenance-process.json',result:'package-maintenance/result.json',stdout:'package-maintenance.stdout.raw',stderr:'package-maintenance.stderr.raw'};
 for(const [name,relative] of Object.entries(names)){const row=binding[name];verifyPin(row);assert.equal(key(row.path),key(path.join(base,relative)),'PACKAGE_REFERENCE_PATH_CHANGED');assert.equal(key(fs.realpathSync(row.path)),key(row.path),'PACKAGE_REFERENCE_REPARSE');}
 const process=read(binding.process.path),result=read(binding.result.path),source='tools/decky_package_maintenance_selftest.ps1';
 assert.deepEqual(result,baselineValue.packageMaintenance,'PACKAGE_REFERENCE_RESULT_NOT_IN_BASELINE');
 const sources=sourceInputs instanceof Map?sourceInputs:sourceMap(sourceInputs),tools=mapTools(toolArtifacts);
 const entry=path.join(repo,source),shell=path.join(expectedEnvironment.SystemRoot??'C:/Windows','System32/WindowsPowerShell/v1.0/powershell.exe');
 assert.equal(key(process.executable.path),key(shell),'PACKAGE_REFERENCE_SHELL_CHANGED');verifyPin(process.executable);assert.equal(tools.get(key(shell)),process.executable.sha256,'PACKAGE_REFERENCE_SHELL_NOT_PREBOUND');
 assert.equal(key(process.source.path),key(entry),'PACKAGE_REFERENCE_SOURCE_PATH');
 assert.equal(process.source.sha256,sources.get(source),'PACKAGE_REFERENCE_SOURCE_NOT_BOUND');verifyPin(process.source);
 assert.equal(process.sourceSha256After,process.source.sha256,'PACKAGE_REFERENCE_SOURCE_CHANGED');assert.equal(process.toolSha256After,process.executable.sha256,'PACKAGE_REFERENCE_SHELL_CHANGED');
 assert.equal(process.sourceReadErrorAfter,null);assert.equal(process.toolReadErrorAfter,null);
 assert.equal(key(process.cwd),key(repo),'PACKAGE_REFERENCE_CWD_CHANGED');assert.deepEqual(process.environment,expectedEnvironment,'PACKAGE_REFERENCE_ENVIRONMENT_CHANGED');
 assert.deepEqual(process.args,['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',entry,'-EvidenceDir',path.join(base,'package-maintenance'),'-TotalBudgetMs','30000','-RunBudgetMs','25000','-CleanupReserveMs','5000'],'PACKAGE_REFERENCE_ORIGINAL_ARGUMENTS');
 for(const [field,value] of Object.entries({status:0,signal:null,error:null,timedOut:false,stageBudgetMs:30000,innerTotalBudgetMs:30000,runBudgetMs:25000,cleanupReserveMs:5000,registeredAdapterBudgetMs:240000}))assert.equal(process[field],value,'PACKAGE_REFERENCE_PROCESS_'+field);
 const begin=Date.parse(process.startedAt),end=Date.parse(process.finishedAt);
 assert(Number.isFinite(begin)&&Number.isFinite(end)&&end>=begin&&begin>=Date.parse(originalProcess.startedAt)&&end<=Date.parse(originalProcess.finishedAt),'PACKAGE_REFERENCE_NOT_CURRENT');
 assert(Number.isFinite(process.elapsedMs)&&process.elapsedMs>=0&&process.elapsedMs<=30000,'PACKAGE_REFERENCE_PROCESS_DEADLINE');
 assert.equal(process.stdoutBytes,binding.stdout.bytes,'PACKAGE_REFERENCE_STDOUT_SIZE');assert.equal(process.stderrBytes,binding.stderr.bytes,'PACKAGE_REFERENCE_STDERR_SIZE');
 assert.equal(process.fixtureCompilerAuthority.manifestSha256,authority.manifestSha256,'PACKAGE_REFERENCE_COMPILER_AUTHORITY');assert.equal(key(process.fixtureCompilerAuthority.compiler),key(authority.compiler),'PACKAGE_REFERENCE_COMPILER_CHANGED');
 assert.deepEqual(mapTools(process.fixtureCompilerAuthority.files),mapTools(authority.files),'PACKAGE_REFERENCE_COMPILER_SCOPE_CHANGED');
 assert.equal(process.fixtureCompilerAuthority.inputs,authority.files.length,'PACKAGE_REFERENCE_COMPILER_COUNT');
 for(const row of process.fixtureCompilerAuthority.files){verifyPin(row);assert.equal(tools.get(key(row.path)),row.sha256,'PACKAGE_REFERENCE_TOOL_NOT_PREBOUND');}
 for(const [field,value] of Object.entries({pass:true,status:'PASS',complete:true,totalBudgetMs:30000,runBudgetMs:25000,cleanupReserveMs:5000,executionScope:'full-original-ordinary-suite',selectedCase:null,realSteamActions:0,hardwareWrites:0,productProcessesLaunched:0,productionGlobalMutexAccessed:false,fixtureMutexNamespaceRemapped:true}))assert.equal(result[field],value,'PACKAGE_REFERENCE_RESULT_'+field);
 assert(result.selectedMutation===null||result.selectedMutation==='','PACKAGE_REFERENCE_SELECTED_MUTATION');
 assert(Number.isInteger(result.declaredOrdinaryCaseCount)&&result.declaredOrdinaryCaseCount>=32,'PACKAGE_REFERENCE_ORDINARY_SCOPE');assert.equal(result.executedOrdinaryCaseCount,result.declaredOrdinaryCaseCount,'PACKAGE_REFERENCE_ORDINARY_OMITTED');assert.deepEqual(result.omittedOrdinaryCases,[],'PACKAGE_REFERENCE_ORDINARY_OMITTED');
 assert(Array.isArray(result.cases)&&result.cases.length===result.count&&result.passed===result.count&&result.count>=result.declaredOrdinaryCaseCount&&result.cases.every(row=>row.pass===true&&row.executed===true),'PACKAGE_REFERENCE_ASSERTION_FAILED');
 const mutations=['allow-downgrade','no-rollback','stale-rollback-snapshot'];assert.deepEqual(Object.keys(result.mutations).sort(),mutations,'PACKAGE_REFERENCE_MUTATIONS_MISSING');assert(Object.values(result.mutations).every(value=>value==='EXPECTED_NONZERO'),'PACKAGE_REFERENCE_MUTATION_NOT_REJECTED');
 assert(Array.isArray(result.mutationRetirement)&&result.mutationRetirement.length===3,'PACKAGE_REFERENCE_RETIREMENT_MISSING');assert.deepEqual(result.mutationRetirement.map(row=>row.name).sort(),mutations,'PACKAGE_REFERENCE_RETIREMENT_DUPLICATE');
 for(const row of result.mutationRetirement){assert.equal(row.executionScope,'selected-original-mutation-assertion');assert.equal(row.plannedCaseCount,1);for(const field of ['jobAssigned','jobCloseSucceeded','exitObserved','sharedDeadline'])assert.equal(row[field],true,'PACKAGE_REFERENCE_MUTATION_NOT_RETIRED');assert.equal(row.timedOut,false);assert.equal(row.error,null);assert(Number.isInteger(row.exitCode)&&row.exitCode!==0,'PACKAGE_REFERENCE_MUTATION_EXIT');}
 const semanticMutations={
  'no-rollback':{case:'mid-transaction copy failure restores all prior committed files',failure:'Failed install did not restore exact prior state'},
  'allow-downgrade':{case:'old YMCC package cannot downgrade a recorded newer Loader',failure:'Expected rejection DOWNGRADE_REFUSED, got '},
  'stale-rollback-snapshot':{case:'rollback revalidates every file after its initial snapshot before restoring it',failure:'Expected rejection ROLLBACK_CONCURRENT_CHANGE, got '}
 };
 assert(Array.isArray(binding.negativeResults)&&binding.negativeResults.length===3,'PACKAGE_REFERENCE_NEGATIVE_RAW_MISSING');assert.deepEqual(binding.negativeResults.map(row=>row.name).sort(),mutations,'PACKAGE_REFERENCE_NEGATIVE_RAW_DUPLICATE');
 for(const row of result.mutationRetirement){
  const expected=semanticMutations[row.name],root=path.join(base,'package-maintenance','mutation-'+row.name);
  assert.equal(row.expectedCase,expected.case,'PACKAGE_REFERENCE_MUTATION_CASE_CHANGED');assert.equal(row.expectedFailure,expected.failure,'PACKAGE_REFERENCE_MUTATION_FAILURE_CHANGED');assert.equal(key(row.independentRoot),key(root),'PACKAGE_REFERENCE_MUTATION_ROOT_CHANGED');
  const raw=binding.negativeResults.find(item=>item.name===row.name).result;verifyPin(raw);assert.equal(key(raw.path),key(path.join(root,'evidence/result.json')),'PACKAGE_REFERENCE_NEGATIVE_PATH_CHANGED');assert.equal(key(fs.realpathSync(raw.path)),key(raw.path),'PACKAGE_REFERENCE_NEGATIVE_REPARSE');
  const negative=read(raw.path);
  for(const [field,value] of Object.entries({complete:true,status:'FAIL',pass:false,executionScope:'selected-original-mutation-assertion',selectedMutation:row.name,selectedCase:expected.case,count:1,passed:0,declaredOrdinaryCaseCount:result.declaredOrdinaryCaseCount,executedOrdinaryCaseCount:1}))assert.equal(negative[field],value,'PACKAGE_REFERENCE_NEGATIVE_'+field);
  assert(Array.isArray(negative.omittedOrdinaryCases)&&negative.omittedOrdinaryCases.length===result.declaredOrdinaryCaseCount-1,'PACKAGE_REFERENCE_NEGATIVE_SCOPE_CHANGED');assert.equal(negative.cases?.length,1,'PACKAGE_REFERENCE_NEGATIVE_ASSERTION_MISSING');
  const failure=negative.cases[0];assert.equal(failure.name,expected.case);assert.equal(failure.executed,true);assert.equal(failure.pass,false);assert.equal(failure.error,expected.failure,'PACKAGE_REFERENCE_NEGATIVE_WRONG_FAILURE');
 }
 assert.equal(result.sourceSha256,sources.get('native/decky_package_maintenance.ps1'),'PACKAGE_REFERENCE_PRODUCT_SOURCE_CHANGED');assert.equal(result.embeddedSha256,sources.get('native/decky_package_maintenance_script.h'),'PACKAGE_REFERENCE_EMBEDDED_SOURCE_CHANGED');
 return {schemaVersion:1,status:'SHARED_REFERENCE',requiredReleaseStep:'decky-package-maintenance',originalProcess:binding.process,originalResult:binding.result,stdout:binding.stdout,stderr:binding.stderr,originalArguments:process.args,originalBudgetMs:30000,originalElapsedMs:process.elapsedMs,ordinaryCases:result.executedOrdinaryCaseCount,semanticNegativeMutations:3,originalNegativeFailuresPreserved:binding.negativeResults,originalProducerExecutionsWithinParent:1,originalProducerExecutionsByThisReference:0,cached:false,fullProductAcceptance:false};
}
export function verifyParentBaselineReference({repoRoot=ROOT,coreRoot=simulationCorePath(),globalResult,globalSha256,parentResult,parentSha256,declaration,expectedEnvironment=baselineEnvironment(),requirePackageMaintenance=false,packageEnvironment=packageMaintenanceEnvironment(),packageFixtureAuthority,requireResources=false,resourceEnvironment=resourceFixtureEnvironment(),requireJavascript=false,javascriptRequest,requireNativeHealth=false,requireNativeRuntime=false}={}){
 const started=performance.now(),repo=fs.realpathSync(repoRoot),core=fs.realpathSync(coreRoot),globalFile=fs.realpathSync(globalResult),parentFile=fs.realpathSync(parentResult),run=path.dirname(globalFile);
 assert(inside(path.join(core,'Evidence'),globalFile),'BASELINE_REFERENCE_GLOBAL_OUTSIDE_CORE');
 assert.equal(key(parentFile),key(path.join(run,'cases',PARENT_ID,'decky-persistent-lifecycle/result.json')),'BASELINE_REFERENCE_FOREIGN_RUN');
 assert.equal(hash(globalFile),globalSha256,'BASELINE_REFERENCE_GLOBAL_CHANGED');assert.equal(hash(parentFile),parentSha256,'BASELINE_REFERENCE_PARENT_CHANGED');
 const global=read(globalFile),parent=read(parentFile),out=path.dirname(parentFile);
 assert.equal(global.verdict?.status,'PASS_SELECTED_SCOPE','BASELINE_REFERENCE_GLOBAL_NOT_PASS');
 const selected=global.results?.filter(row=>row.id===PARENT_ID);assert.equal(selected?.length,1,'BASELINE_REFERENCE_PARENT_NOT_UNIQUE');assert.equal(selected[0].status,'PASS');assert.equal(selected[0].exitCode,0);
 assert.equal(parent.id,PARENT_ID);assert.equal(parent.status,'PASS');for(const field of ['passed','fullPersistentAcceptance','sourceUnchanged','toolsUnchanged','metadataIntegrity'])assert.equal(parent[field],true,'BASELINE_REFERENCE_PARENT_'+field);
 const start=Date.parse(parent.startedAt),finish=Date.parse(parent.finishedAt);assert(Number.isFinite(start)&&Number.isFinite(finish)&&finish>=start&&start>=Date.parse(global.startedAt),'BASELINE_REFERENCE_PARENT_TIME');
 const decl=declaration??parentDeclaration(repo);
 const original=parent.originalUpdateRecovery;assert.equal(original?.sourceVerified,true);assert.equal(original.reused,false,'BASELINE_REFERENCE_IMPORTED_PREVIOUS_BASELINE');
 const process=original.process,executions=parent.processes.filter(p=>p.id==='original.plugins.decky-update-recovery');assert.equal(executions.length,1,'BASELINE_REFERENCE_EXECUTION_NOT_UNIQUE');assert.deepEqual(executions[0],process,'BASELINE_REFERENCE_PROCESS_MISMATCH');
 const processFile=path.join(out,'processes/original.plugins.decky-update-recovery.json');assert.deepEqual(read(processFile),process,'BASELINE_REFERENCE_PROCESS_FILE_MISMATCH');
 assert(Date.parse(process.startedAt)>=start&&Date.parse(process.finishedAt)<=finish&&Date.parse(process.finishedAt)>=Date.parse(process.startedAt),'BASELINE_REFERENCE_PROCESS_NOT_CURRENT');
 verifyPin(original.raw);assert(inside(path.join(core,'Evidence'),fs.realpathSync(original.raw.path)),'BASELINE_REFERENCE_RAW_OUTSIDE_CORE');assert.deepEqual(process.originalResult,original.raw,'BASELINE_REFERENCE_RAW_NOT_BOUND');
 assert(parent.rawEvidence.some(p=>key(p.path)===key(original.raw.path)&&p.sha256===original.raw.sha256),'BASELINE_REFERENCE_RAW_NOT_IN_PARENT');
 const entry=path.join(repo,'tools/ymcc_sim/global/decky-update-recovery/run.mjs');assert.deepEqual(process.arguments,[entry,'--evidence-dir',path.dirname(original.raw.path)],'BASELINE_REFERENCE_ORIGINAL_ARGUMENTS');assert.equal(key(process.cwd),key(repo));
 assert.equal(key(process.program.path),key(processExec));for(const row of [process.program,process.stdout,process.stderr])verifyPin(row);
 assert.equal(key(process.stdout.path),key(path.join(out,'processes/original.plugins.decky-update-recovery.stdout.bin')));assert.equal(key(process.stderr.path),key(path.join(out,'processes/original.plugins.decky-update-recovery.stderr.bin')));
 assert.deepEqual(process.baselineEnvironment,expectedEnvironment,'BASELINE_REFERENCE_ENVIRONMENT_CHANGED');
 const sources=sourceMap(parent.sourceInputs);assert.deepEqual([...sources.keys()].sort(),[...new Set(decl.evidenceInputs)].sort(),'BASELINE_REFERENCE_SOURCE_DECLARATION_CHANGED');
 for(const [relative,sha256] of sources){assert(!path.isAbsolute(relative)&&!relative.split(/[\\/]/).some(p=>!p||p==='.'||p==='..'),'BASELINE_REFERENCE_SOURCE_PATH_INVALID');const file=path.join(repo,relative);assert.equal(hash(file),sha256,'BASELINE_REFERENCE_SOURCE_CHANGED:'+relative);}
 const alias='tools/decky_update_recovery_selftest.mjs';assert.equal(sources.get(alias),REVIEWED_RELEASE_ALIAS_SHA256,'BASELINE_REFERENCE_ALIAS_UNREVIEWED');assert.equal(hash(path.join(repo,alias)),REVIEWED_RELEASE_ALIAS_SHA256);
 const originalValue=read(original.raw.path);validateBaselineReceipt(originalValue,process,sources,decl.baselineRequiredSources);
 const tools=mapTools(parent.toolArtifacts);assert.deepEqual(mapTools(process.toolArtifacts),tools,'BASELINE_REFERENCE_PROCESS_TOOL_SCOPE_CHANGED');assert.deepEqual([...tools.keys()].sort(),[...new Set(decl.artifactInputs.map(key))].sort(),'BASELINE_REFERENCE_TOOL_DECLARATION_CHANGED');
 for(const tool of parent.toolArtifacts)verifyPin(tool);
 const metadata=verifyParentMetadata(out,parent);
 const packageMaintenance=requirePackageMaintenance?verifyPackageMaintenanceReference({repoRoot:repo,baselineResult:original.raw.path,baselineValue:originalValue,originalProcess:process,sourceInputs:sources,toolArtifacts:parent.toolArtifacts,expectedEnvironment:packageEnvironment,...(packageFixtureAuthority?{authority:packageFixtureAuthority}:{})}):null;
 const resources=requireResources?Object.fromEntries(['cache','worker'].map(family=>[family,verifyNativeResourceReference({family,repoRoot:repo,coreRoot:core,baselineResult:original.raw.path,baselineValue:originalValue,originalProcess:process,baselineProcessFile:processFile,sourceInputs:sources,toolArtifacts:parent.toolArtifacts,expectedEnvironment:resourceEnvironment})])):null;
 const javascript=requireJavascript?verifyJavascriptReferences({repo,core,out,parent,global,declaration:decl,sources,tools,request:javascriptRequest}):null;
 const nativeHealth=requireNativeHealth?verifyNativeHealthReference({repoRoot:repo,coreRoot:core,parentResult:parentFile,parent,sourceInputs:sources,toolArtifacts:parent.toolArtifacts}):null;
 const nativeRuntime=requireNativeRuntime?verifyNativeRuntimeReference({repoRoot:repo,coreRoot:core,parentResult:parentFile,parent,sourceInputs:sources,toolArtifacts:parent.toolArtifacts}):null;
 return {...(requireNativeHealth?{nativeHealth}:{}),...(requireNativeRuntime?{nativeRuntime}:{}),...(requireJavascript?{javascript}:{}),schemaVersion:1,status:'SHARED_REFERENCE',canonicalTestId:'plugins.decky-update-recovery',requiredReleaseStep:'decky_update_recovery_selftest',parentId:PARENT_ID,parentResult:pin(parentFile),globalResult:pin(globalFile),originalResult:original.raw,originalProcess:pin(processFile),originalProgram:process.program,originalArguments:process.arguments,originalBudgetMs:process.originalBudgetMs,originalTimeoutMs:process.timeoutMs,originalElapsedMs:process.elapsedMs,originalNativeChecks:originalValue.nativeChecks,packageMaintenance,resources,baselineEnvironment:process.baselineEnvironment,sourceInputs:parent.sourceInputs.length,toolArtifacts:parent.toolArtifacts.length,metadata,originalProducerExecutionsWithinParent:1,originalProducerExecutionsByThisReference:0,verificationExecuted:true,verificationWallMs:performance.now()-started,cached:false,fullProductAcceptance:false};
}
// The caller supplies the actual object returned by this runAdapters invocation, never a historical JSON input.
// This completed-parent subset is not a verdict for the still-running global Full selection.
export function verifyCurrentRunParentBaselineReference({repoRoot,coreRoot,runDir,runStartedAt,parentExecution,declaration,expectedEnvironment=baselineEnvironment()}={}){
 const started=performance.now();
 for(const [name,file]of Object.entries({repoRoot,coreRoot,runDir}))assert(typeof file==='string'&&path.isAbsolute(file),'CURRENT_RUN_BASELINE_REFERENCE_ABSOLUTE_'+name);
 const repo=fs.realpathSync(repoRoot),core=fs.realpathSync(coreRoot),run=fs.realpathSync(runDir);
 assert.equal(key(run),key(runDir),'CURRENT_RUN_BASELINE_REFERENCE_RUN_REPARSE');
 assert(inside(path.join(core,'Evidence'),run),'CURRENT_RUN_BASELINE_REFERENCE_RUN_OUTSIDE_CORE');
 const time=(value,name)=>{
  assert((typeof value==='string'&&value.length>0)||(typeof value==='number'&&Number.isSafeInteger(value)),'CURRENT_RUN_BASELINE_REFERENCE_TIME_REQUIRED_'+name);
  const result=typeof value==='number'?value:Date.parse(value);assert(Number.isFinite(result)&&result>=0,'CURRENT_RUN_BASELINE_REFERENCE_TIME_INVALID_'+name);return result;
 };
 const runStart=time(runStartedAt,'runStartedAt'),now=Date.now();
 assert(parentExecution&&typeof parentExecution==='object'&&!Array.isArray(parentExecution),'CURRENT_RUN_BASELINE_REFERENCE_EXECUTION_REQUIRED');
 assert.equal(parentExecution.id,PARENT_ID,'CURRENT_RUN_BASELINE_REFERENCE_ADAPTER_ID');
 assert.equal(parentExecution.status,'PASS','CURRENT_RUN_BASELINE_REFERENCE_ADAPTER_NOT_PASS');
 assert.equal(parentExecution.exitCode,0,'CURRENT_RUN_BASELINE_REFERENCE_ADAPTER_EXIT');
 for(const field of ['sourceChanged','candidateArtifactChanged','timedOut','outputTruncated','requiredEvidenceMissing','requiredEvidenceFailed'])assert.equal(parentExecution[field],false,'CURRENT_RUN_BASELINE_REFERENCE_ADAPTER_'+field);
 for(const field of ['signal','launchError'])assert.equal(parentExecution[field],null,'CURRENT_RUN_BASELINE_REFERENCE_ADAPTER_'+field);
 for(const field of ['buildError','missingInputs'])assert(!parentExecution[field],'CURRENT_RUN_BASELINE_REFERENCE_ADAPTER_'+field);
 assert(Number.isSafeInteger(parentExecution.pid)&&parentExecution.pid>0,'CURRENT_RUN_BASELINE_REFERENCE_PID_REQUIRED');
 assert.equal(parentExecution.source,'tools/ymcc_sim/global/decky-persistent-lifecycle/current-parent-global.mjs','CURRENT_RUN_BASELINE_REFERENCE_ADAPTER_SOURCE');
 const adapterStart=time(parentExecution.startedAt,'adapterStartedAt'),adapterFinish=time(parentExecution.finishedAt,'adapterFinishedAt');
 assert(runStart<=adapterStart&&adapterStart<=adapterFinish&&adapterFinish<=now,'CURRENT_RUN_BASELINE_REFERENCE_ADAPTER_NOT_CURRENT');
 const caseDir=path.join(run,'cases',PARENT_ID);
 assert(typeof parentExecution.evidenceDir==='string'&&path.isAbsolute(parentExecution.evidenceDir),'CURRENT_RUN_BASELINE_REFERENCE_EVIDENCE_REQUIRED');
 assert.equal(key(parentExecution.evidenceDir),key(caseDir),'CURRENT_RUN_BASELINE_REFERENCE_FOREIGN_EVIDENCE');
 assert.equal(key(fs.realpathSync(parentExecution.evidenceDir)),key(caseDir),'CURRENT_RUN_BASELINE_REFERENCE_EVIDENCE_REPARSE');
 const parentFile=path.join(caseDir,'decky-persistent-lifecycle/result.json');
 assert(Array.isArray(parentExecution.structuredEvidence),'CURRENT_RUN_BASELINE_REFERENCE_STRUCTURED_EVIDENCE_REQUIRED');
 const rows=parentExecution.structuredEvidence.filter(row=>typeof row?.path==='string'&&path.isAbsolute(row.path)&&key(row.path)===key(parentFile));
 assert(rows.length>0,'CURRENT_RUN_BASELINE_REFERENCE_PARENT_PIN_MISSING');
 assert.equal(rows.length,1,'CURRENT_RUN_BASELINE_REFERENCE_PARENT_PIN_NOT_UNIQUE');
 const observed=rows[0];assert.equal(observed.failed,false,'CURRENT_RUN_BASELINE_REFERENCE_PARENT_PIN_FAILED');
 assert(/^[a-f0-9]{64}$/.test(observed.sha256??''),'CURRENT_RUN_BASELINE_REFERENCE_PARENT_PIN_INVALID');
 assert(fs.existsSync(parentFile),'CURRENT_RUN_BASELINE_REFERENCE_PARENT_RESULT_MISSING');
 assert.equal(key(fs.realpathSync(parentFile)),key(parentFile),'CURRENT_RUN_BASELINE_REFERENCE_PARENT_REPARSE');verifyPin(observed);
 let parent;try{parent=read(parentFile);}catch{assert.fail('CURRENT_RUN_BASELINE_REFERENCE_PARENT_RESULT_INVALID_JSON');}
 assert(parent&&typeof parent==='object'&&!Array.isArray(parent),'CURRENT_RUN_BASELINE_REFERENCE_PARENT_RESULT_INVALID_SCHEMA');
 assert.equal(parent.id,PARENT_ID,'CURRENT_RUN_BASELINE_REFERENCE_PARENT_ID');
 assert.equal(parent.status,'PASS','CURRENT_RUN_BASELINE_REFERENCE_PARENT_NOT_PASS');
 for(const field of ['passed','fullPersistentAcceptance','sourceUnchanged','toolsUnchanged','metadataIntegrity'])assert.equal(parent[field],true,'CURRENT_RUN_BASELINE_REFERENCE_PARENT_'+field);
 const parentStart=time(parent.startedAt,'parentStartedAt'),parentFinish=time(parent.finishedAt,'parentFinishedAt');
 assert(adapterStart<=parentStart&&parentStart<=parentFinish&&parentFinish<=adapterFinish,'CURRENT_RUN_BASELINE_REFERENCE_RAW_OUTSIDE_ADAPTER_WINDOW');
 const scopeFile=path.join(run,'parent-baseline-reference-scope.json');
 const subset={schemaVersion:1,tool:'ymcc_sim-global',mode:'run',repoRoot:repo,
  scope:'current-run-completed-parent-subset',scopeMeaning:'Only the actually completed parent adapter; not the entire global Full verdict',
  runStartedAt,startedAt:new Date(runStart).toISOString(),finishedAt:parentExecution.finishedAt,
  plannedTests:1,results:[parentExecution],verdict:{status:'PASS_SELECTED_SCOPE',exitCode:0},
  globalFullPassed:false,fullProductAcceptance:false,referenceVerificationIsSeparate:true};
 fs.writeFileSync(scopeFile,JSON.stringify(subset,null,2)+'\n',{flag:'wx'});
 // Leave the fresh subset on verification failure; never delete, overwrite, retry or launch a fallback producer.
 const reference=verifyParentBaselineReference({repoRoot:repo,coreRoot:core,globalResult:scopeFile,globalSha256:hash(scopeFile),
  parentResult:parentFile,parentSha256:observed.sha256,declaration,expectedEnvironment});
 return {...reference,referenceScope:'current-run-completed-parent-subset',scopeResult:reference.globalResult,
  parentExecutionWindow:{pid:parentExecution.pid,startedAt:parentExecution.startedAt,finishedAt:parentExecution.finishedAt},
  runStartedAt,globalFullPassed:false,originalProducerExecutionsByThisCall:0,
  parentVerificationWallMs:reference.verificationWallMs,verificationWallMs:performance.now()-started};
}
const processExec=process.execPath;
export function main(args){
 const options={};for(let i=0;i<args.length;i+=2){assert(['--global-result','--global-sha256','--parent-result','--parent-sha256','--out','--javascript-request','--native-health-reference','--native-runtime-reference'].includes(args[i])&&args[i+1]&&!options[args[i]],'BASELINE_REFERENCE_ARGUMENTS_INVALID');options[args[i]]=args[i+1];}
 if(options['--native-health-reference'])assert.equal(options['--native-health-reference'],'required','BASELINE_NATIVE_HEALTH_REFERENCE_MODE_INVALID');
 if(options['--native-runtime-reference'])assert.equal(options['--native-runtime-reference'],'required','BASELINE_NATIVE_RUNTIME_REFERENCE_MODE_INVALID');
 assert(['--global-result','--global-sha256','--parent-result','--parent-sha256','--out'].every(name=>options[name]),'BASELINE_REFERENCE_ARGUMENTS_REQUIRED');
 const output=path.resolve(options['--out']);assert(inside(path.dirname(path.resolve(options['--global-result'])),output),'BASELINE_REFERENCE_OUTPUT_SCOPE');assert(!fs.existsSync(output),'BASELINE_REFERENCE_FRESH_OUTPUT_REQUIRED');
 const result=verifyParentBaselineReference({globalResult:options['--global-result'],globalSha256:options['--global-sha256'],parentResult:options['--parent-result'],parentSha256:options['--parent-sha256'],requirePackageMaintenance:true,requireResources:true,requireNativeHealth:options['--native-health-reference']==='required',requireNativeRuntime:options['--native-runtime-reference']==='required',...(options['--javascript-request']?{requireJavascript:true,javascriptRequest:read(options['--javascript-request'])}:{})});fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output,JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result));return 0;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))try{process.exitCode=main(process.argv.slice(2));}catch(error){console.error('BASELINE_REFERENCE_FAILED '+error.stack);process.exitCode=10;}
