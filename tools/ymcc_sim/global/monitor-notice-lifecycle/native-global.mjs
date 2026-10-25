import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
import {begin,source,generated,verify,receipt,sha,save,readJson,run,inside} from '../../../fixtures/radial_private/scope.mjs';
import {MODULE,NATIVE_ID,NATIVE_INPUTS,NATIVE_QUOTE_SOURCES} from './native-adapter.mjs';

const CASE_IDS=Object.freeze([
 'retirement-before-create-revokes-native-admission-without-a-window',
 'exact-native-retirement-destroys-two-owned-windows-timers-and-controller-reference',
 'foreign-nonce-and-future-generation-cannot-destroy-owned-resources',
 'obsolete-retirement-cannot-close-the-newer-owner',
 'ordinary-close-revokes-create-before-window-callbacks',
 'first-destroywindow-failure-keeps-owner-for-exact-retirement-retry',
 'input-window-destruction-failure-also-retains-and-retries-owner',
 'controller-close-error-still-releases-owned-com-reference-and-windows',
 'repeated-retirement-is-idempotent-and-native-generation-never-wraps',
 'exact-main-reserve-create-retire-handlers-enforce-native-lease-and-request-shape',
]);
const MUTATIONS=Object.freeze({
 'skip-retired-create-admission':{
  failures:{[CASE_IDS[0]]:'Late retired create regained native admission',[CASE_IDS[9]]:'Exact main request admission was bypassed'},
  needle:'   if(config.noticeOnly&&(!lease||lease!=noticeLeaseSerial||nonce!=noticeLeaseNonce||noticeLeaseRetired))throw std::runtime_error("Notice owner lease retired or replaced");',
 },
 'ignore-destroy-failure':{
  // One original guard retains ownership for either HWND; both original cases fail when it is omitted.
  failures:{[CASE_IDS[5]]:'Failed DestroyWindow was reported as success',[CASE_IDS[6]]:'Failed input window destruction reported success'},
  needle:'   if(s->hwnd&&IsWindow(s->hwnd)||s->inputWindow&&IsWindow(s->inputWindow)){s->reason="owned-window-destruction-unconfirmed";return false;}',
 },
});
const POSITIVE_STATUS='ACTUAL_PRODUCTION_CARRIER_HIDDEN_HWND_TIMER_COM_ABI_AND_EXACT_MAIN_LEASE10_PASS';
const NEGATIVE_STATUS='NATIVE_LEASE_SINGLE_GUARD_NEGATIVE_DETECTED_ORIGINAL_FAILURES_RETAINED';

// Consumes actual native output. A counterexample is accepted only by its complete
// original failed-case/error mask; no result bytes or consumer assertions change.
export function validateNativeResult(raw,execution,mutation=null){
 assert(mutation===null||Object.hasOwn(MUTATIONS,mutation),'UNKNOWN_MUTATION');
 assert.equal(execution.signal,null,'NATIVE_SIGNAL');
 assert.equal(execution.killed,false,'NATIVE_NOT_NATURAL_EXIT');
 assert.deepEqual(raw.cases.map(row=>row.id),CASE_IDS,'ORIGINAL_TEN_CASES_CHANGED');
 assert.equal(raw.counts.cases,10);
 assert(raw.cases.every(row=>typeof row.passed==='boolean'),'NATIVE_CASE_VERDICT_REQUIRED');
 const failed=raw.cases.filter(row=>!row.passed);
 const expected=mutation?MUTATIONS[mutation].failures:{};
 assert.deepEqual(failed.map(row=>row.id).sort(),Object.keys(expected).sort(),'EXACT_NATIVE_FAILURE_MASK');
 for(const row of failed)assert.equal(row.error,expected[row.id],'EXACT_NATIVE_FAILURE_REASON');
 assert.equal(raw.counts.passed,10-failed.length,'NATIVE_PASSED_COUNT');
 assert.equal(raw.status,mutation?'FAIL':'PASS');
 assert.equal(execution.exitCode,mutation?2:0,'ORIGINAL_NATIVE_EXIT_CODE');
 assert.equal(raw.unownedDestroyAttempts,0);
 for(const key of ['hiddenOwnWindowsOnly','productionCarrierExecuted','controllerAbiTestDouble'])assert.equal(raw[key],true,key);
 for(const key of ['realWebView2ControllerExecuted','deviceIo','normalYmccStarted','operatingSystemSleep','manualSleepAcceptanceReady'])assert.equal(raw[key],false,key);
 if(!mutation){assert.equal(raw.remainingOwnedWindows,0);assert.equal(raw.controllerLive,0);}
 return {failedCaseIds:failed.map(row=>row.id),counts:raw.counts,remainingOwnedWindows:raw.remainingOwnedWindows,controllerLive:raw.controllerLive,unownedDestroyAttempts:raw.unownedDestroyAttempts};
}
function pin(file){return {path:file,sha256:sha(file)};}
function ownedFile(root,file){assert(inside(root,file)&&inside(fs.realpathSync(root),fs.realpathSync(file)),'RUN_EVIDENCE_ESCAPE');return file;}
function validateReceipt(scope,dir,state,graph){
 assert.equal(state.out,path.resolve(dir));
 assert.equal(graph.pass,true);assert.equal(state.exe.path,graph.executable);
 assert.equal(sha(ownedFile(dir,state.exe.path)),state.exe.sha256);
 assert.equal(state.exe.sha256,graph.executableSha256);
 assert.deepEqual(state.receipt.toolArtifacts,scope.toolArtifacts,'ORIGINAL_TOOL_DECLARATION_CHANGED');
 for(const [name,digest] of Object.entries(state.receipt.sourceHashes)){
  const file=path.isAbsolute(name)?ownedFile(dir,name):path.join(scope.repo,name);
  assert.equal(sha(file),digest,'ORIGINAL_RECEIPT_SOURCE_CHANGED:'+name);
 }
 const declared=new Set(NATIVE_QUOTE_SOURCES),before=new Map(state.beforeNativeInputs.map(row=>[fs.realpathSync(row.path).toLowerCase(),row]));
 for(const row of state.beforeNativeInputs){
  if(inside(dir,row.path)){ownedFile(dir,row.path);}else assert(declared.has(path.relative(scope.repo,row.path).replaceAll('\\','/')),'UNDECLARED_QUOTED_SOURCE:'+row.path);
  assert.equal(sha(row.path),row.sha256,'BEFORE_COMPILE_SOURCE_CHANGED');
 }
 for(const row of graph.nativeInputs.filter(row=>row.role==='source')){
  const original=before.get(fs.realpathSync(row.path).toLowerCase());assert(original,'SOURCE_NOT_BOUND_BEFORE_COMPILE');assert.equal(original.sha256,row.sha256);
 }
 const slice=state.exactHandlerSlice,main=path.join(scope.repo,'native/main.cpp'),text=fs.readFileSync(main,'utf8');
 const first=text.indexOf('    ipc_on("monitorNotice.reserve"'),last=text.indexOf('    ipc_on("monitorNotice.frame"',first);
 assert(first>=0&&last>first);assert.equal(slice.source,main);assert.equal(slice.sourceSha256,sha(main));
 assert.equal(slice.start,first);assert.equal(slice.end,last);
 assert.equal(fs.readFileSync(ownedFile(dir,slice.generated),'utf8'),text.slice(first,last),'HANDLER_SLICE_NOT_VERBATIM');
 assert.equal(sha(slice.generated),slice.sha256);
 const helper=state.exactDiagnosticHelperSlice,marker='// Fixed diagnostic state shared by every template instantiation; no resource ownership.';assert.equal(text.split(marker).length,2);const helperStart=text.indexOf(marker),helperTemplate=text.indexOf('template <typename BuildFields>',helperStart),helperEnd=text.indexOf('\n}',helperTemplate)+2;assert(helper&&helperTemplate>helperStart&&helperEnd>helperTemplate,'EXACT_DIAGNOSTIC_HELPER_GROUP_REQUIRED');assert.equal(helper.source,main);assert.equal(helper.sourceSha256,sha(main));assert.equal(helper.start,helperStart);assert.equal(helper.end,helperEnd);assert.equal(fs.readFileSync(ownedFile(dir,helper.generated),'utf8'),text.slice(helperStart,helperEnd),'DIAGNOSTIC_HELPER_GROUP_NOT_VERBATIM');assert.equal(sha(helper.generated),helper.sha256);
 assert.equal(state.toolProof.maxArtifactInputs,3072);assert(state.toolProof.artifactCount<=3072);
 assert.equal(state.toolProof.nodeGraphSha256,scope.node.graphSha256);
}

export async function main(args=process.argv.slice(2)){
 assert(args.length===2&&args[0]==='--evidence-dir'&&path.isAbsolute(args[1]),'USE --evidence-dir ABS_NEW_DIRECTORY');
 const repo=path.resolve(import.meta.dirname,'../../../..'),out=path.resolve(args[1]);
 const scope=begin(repo,out,MODULE+'/native-global.mjs');
 const result={schemaVersion:1,id:NATIVE_ID,status:'FAIL',exitCode:2,startedAt:new Date().toISOString(),
  lifecycleComponent:{featureId:'monitor.notice-host',persistence:'persistent',role:'native-exit-component',parentAdapterId:'lifecycle.monitor-notice-host'},
  nativeExitComponentOnly:true,fullPersistentRecoveryAcceptance:false,fullProductAcceptance:false,manualSleepAcceptanceReady:false,
  realWebView2ControllerExecuted:false,normalYmccStarted:false,deviceIo:false,operatingSystemSleep:false,cases:[],nativeRuns:[]};
 try{
  for(const member of NATIVE_INPUTS)source(scope,path.join(repo,member));
  for(const mutation of [null,...Object.keys(MUTATIONS)]){
   // Keep the original MSVC TEMP/response path below its legacy GUID-temp limit.
   const name=mutation??'positive',leaf=mutation===null?'p':mutation==='skip-retired-create-admission'?'r':'d',dir=path.join(out,leaf),startedAt=new Date().toISOString();
   const command=[process.execPath,path.join(repo,MODULE,'native-lease-selftest.mjs'),'--out',dir,...(mutation?['--mutation',mutation]:[])];
   let child;try{child=await run(command[0],command.slice(1),{cwd:repo,windowsHide:true,timeout:180000,maxBuffer:8e6});}catch(error){child=error;}
   const log=path.join(out,name+'.log');fs.writeFileSync(log,(child.stdout??'')+(child.stderr??''));
   const execution={exitCode:child instanceof Error?(Number.isInteger(child.code)?child.code:null):0,signal:child.signal??null,killed:child.killed??false};
   const row={name,mutation,command,startedAt,finishedAt:new Date().toISOString(),selftestExecution:execution,log:pin(log)};result.nativeRuns.push(row);
   assert.equal(execution.exitCode,0,'ORIGINAL_SELFTEST_FAILED:'+name);assert.equal(execution.signal,null);assert.equal(execution.killed,false);
   const stateFile=ownedFile(dir,path.join(dir,'NATIVE-LEASE-PROCESS.json')),rawFile=ownedFile(dir,path.join(dir,'native-result.json'));
   const state=readJson(stateFile),raw=readJson(rawFile),graphFile=ownedFile(dir,path.join(dir,'native-build-graph.json')),graph=readJson(graphFile);
   row.originalProcess=pin(stateFile);row.originalResult=pin(rawFile);row.nativeBuildGraph=pin(graphFile);row.originalNativeStatus=raw.status;row.nativeExecution=state.execution;
   assert.equal(state.status,mutation?NEGATIVE_STATUS:POSITIVE_STATUS,'ORIGINAL_SELFTEST_STATUS');
   assert.equal(state.result.path,rawFile);assert.equal(state.result.sha256,sha(rawFile));
   validateReceipt(scope,dir,state,graph);
   if(mutation){
    const original=path.join(repo,'native/monitor_desktop_hud.h'),changed=ownedFile(dir,state.mutation.changed.path),text=fs.readFileSync(original,'utf8'),needle=MUTATIONS[mutation].needle;
    assert.equal(state.mutation.kind,mutation);assert.equal(state.mutation.original.path,original);assert.equal(state.mutation.original.sha256,sha(original));
    assert.equal(text.split(needle).length,2);assert.equal(state.mutation.needle,needle);
    assert.equal(fs.readFileSync(changed,'utf8'),text.replace(needle,'   /* exact single negative-control guard omitted */'),'NOT_SINGLE_GUARD_MUTATION');
    assert.equal(state.mutation.changed.sha256,sha(changed));
   }
   row.observations=validateNativeResult(raw,state.execution,mutation);
   if(mutation)assert.deepEqual(state.expectedNegativeFailures,row.observations.failedCaseIds);
   row.toolProof=state.toolProof;row.executable=state.exe;row.exactHandlerSlice=state.exactHandlerSlice;
   result.cases.push({id:mutation?'exact-negative-mask-'+mutation:'original-native-positive10',passed:true});
   for(const [file,role] of [[stateFile,'unchanged original selftest process receipt'],[rawFile,'unchanged original native result; negative FAIL retained'],[graphFile,'original compiler input/link graph'],[log,'actual original selftest stdout/stderr']])generated(scope,file,role);
  }
  result.toolProof=verify(scope);result.status='PASS';result.exitCode=0;
 }catch(error){result.error=String(error.stack??error);result.cases.push({id:'native-global-contract',passed:false});}
 result.finishedAt=new Date().toISOString();result.sourceHashes=scope.sourceHashes;
 result.counts={cases:result.cases.length,passed:result.cases.filter(row=>row.passed).length};
 result.receipt=receipt(scope,{boundary:'Native exit/admission component of persistent monitor.notice-host; full recovery/real WebView2/installed app not accepted.'});
 save(path.join(out,'result.json'),result);
 console.log(JSON.stringify({status:result.status,id:NATIVE_ID,out,nativeRuns:result.nativeRuns.map(row=>({name:row.name,originalNativeStatus:row.originalNativeStatus,nativeExecution:row.nativeExecution,observations:row.observations}))}));
 return result.exitCode;
}
if(process.argv[1]&&pathToFileURL(path.resolve(process.argv[1])).href===import.meta.url){
 main().then(code=>{process.exitCode=code;}).catch(error=>{console.error(error.stack??error);process.exitCode=2;});
}