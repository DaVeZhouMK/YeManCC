import {simulationCorePath} from '../release-host-environment.mjs';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {pathToFileURL} from 'node:url';
import {OWNED_ID,OWNED_SOURCES,OWNED_INPUTS,ownedToolDeclaration} from './owned-adapter.mjs';

// Reviewed registration/result pairs from the unchanged original native producer.
// Added source cases require review; counts derive from this single contract.
export const ORIGINAL_CASES=Object.freeze([
 {
  "registrationId": "native.typed-runtime-live-watch",
  "id": "native.typed-runtime-drains-live-watch-without-extra-reader-thread"
 },
 {
  "registrationId": "native.typed-stalled-binding-stop",
  "id": "native.typed-stalled-binding-stop-before-fixture-release"
 },
 {
  "registrationId": "native.typed-stalled-binding-descendants",
  "id": "native.typed-stalled-binding-descendants-stop-before-release"
 },
 {
  "registrationId": "native.typed-rejected-final-admission",
  "id": "native.typed-rejected-final-admission-releases-watch-before-off"
 },
 {
  "registrationId": "native.typed-ready-rebind",
  "id": "native.typed-ready-rebind-keeps-original-owned-loader"
 },
 {
  "registrationId": "native.typed-retained-ready-rebind",
  "id": "native.typed-retained-ready-rebind-never-starts-loader"
 }
]);
export const ORIGINAL_IDS=Object.freeze(ORIGINAL_CASES.map(row=>row.id));
export const ORIGINAL_REGISTRATION_IDS=Object.freeze(ORIGINAL_CASES.map(row=>row.registrationId));
// Select the unchanged six-case parent scope explicitly. New producer cases
// retain their independent full entry and cannot silently alter this receipt.
export const ORIGINAL_SELECTION=ORIGINAL_REGISTRATION_IDS.join(',');
export function originalReadyConsumer(source,{write=false}={}){
 const origin='native/main.cpp',file=path.join(source,origin),text=fs.readFileSync(file,'utf8');
 const match=text.match(/static bool deckySidebarResumeAfterIntent\(unsigned long long generation\) \{[\s\S]*?\n\}/);
 assert(match,'ORIGINAL_READY_CONSUMER_REQUIRED');assert.equal(text.split(match[0]).length-1,1,'ORIGINAL_READY_CONSUMER_UNIQUE');
 const target=path.join(source,'native/decky_wake_ready_consumer.inc'),expected=match[0]+'\n';
 if(write)fs.writeFileSync(target,expected);assert.equal(fs.readFileSync(target,'utf8'),expected,'ORIGINAL_READY_CONSUMER_BYTES');
 return {...pin(target),origin,originSha256:sha(fs.readFileSync(file)),slice:{begin:match.index,end:match.index+match[0].length,appendNewline:true},bytePreserved:true};
}

const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const pin=file=>({path:file,sha256:sha(fs.readFileSync(file))});
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const key=file=>path.resolve(file).toLowerCase();
const within=(root,file)=>key(file).startsWith(key(root)+path.sep);
// The legacy25/7 aggregation consumes six named observations. Execute the
// current producer without a selection so dynamic and new registrations remain
// real gates; a projection is only an aggregation view, never a passing subset.
export function originalAggregateCases(value){
 return ORIGINAL_CASES.map(spec=>{const rows=value.cases.filter(row=>row.id===spec.id||(row.passed===false&&row.id===spec.registrationId));assert.equal(rows.length,1,'ORIGINAL_AGGREGATE_CASE_MISSING_OR_DUPLICATE:'+spec.id);return rows[0];});
}
export function validateOriginalResult(value,run){
 assert.equal(value.schema,'ymcc.typed-binding-runtime-gate.v1');
 assert(Number.isSafeInteger(value.tests)&&value.tests>=ORIGINAL_CASES.length,'ORIGINAL_TYPED_CASES_REQUIRED');assert.equal(value.tests,value.cases.length);assert.equal(value.deadlineMs,3200);
 assert.equal(value.productCallbackMode,'typed-only; legacy macro absent');assert.equal(value.realSteamOrDeviceActions,0);
 assert.equal(value.originalHistoricalFailurePreserved,true);assert.equal(value.fieldWakeOrS4Verified,false);assert.equal(value.hostPackagePassed,false);
 assert.equal(new Set(value.cases.map(row=>row.id)).size,value.cases.length,'ORIGINAL_TYPED_DUPLICATE_CASE');
 assert(value.cases.every(row=>typeof row.passed==='boolean'),'TYPED_CASE_VERDICT_REQUIRED');
 const selected=originalAggregateCases(value),passed=value.cases.filter(row=>row.passed).length;
 assert.equal(value.passedCount,passed);assert.equal(value.passed,passed===value.cases.length);
 const complete=value.selection==='';
 if(complete)assert.equal(run.selection,null,'ORIGINAL_FULL_PRODUCER_COMMAND_REQUIRED');
 else{assert.equal(value.selection,ORIGINAL_SELECTION);assert.equal(run.selection,ORIGINAL_SELECTION);assert.equal(value.tests,ORIGINAL_CASES.length);}
 assert.equal(run.runExit,value.passed?0:1);assert.equal(run.error,null);assert.equal(run.compilerExit,0);assert.deepEqual(run.changedOriginalInputs,[]);assert.equal(run.status,value.passed?'pass':'fail');
 return {cases:value.cases.length,passed,failed:value.cases.length-passed,failedCaseIds:value.cases.filter(row=>!row.passed).map(row=>row.id),completeCurrentProducer:complete,aggregateCases:selected.length,additionalCases:value.cases.length-selected.length};
}
// Reuse is deliberately confined to the sibling parent in this exact global
// invocation. Standalone calls keep executing the original producer. This is
// component evidence consumption, not a cached/previous-run business PASS.
export function sameRunOriginal(out,repo){
 if(path.basename(out)!=='decky-owned-runtime-lifecycle')return null;
 const casesRoot=path.resolve(out,'../..');if(path.basename(casesRoot)!=='cases')return null;
 const parentFile=path.join(casesRoot,'plugins.decky-persistent-lifecycle/decky-persistent-lifecycle/result.json');if(!fs.existsSync(parentFile))return null;
 const parent=read(parentFile);assert.equal(parent.id,'plugins.decky-persistent-lifecycle','SAME_RUN_PARENT_ID');
 for(const relative of OWNED_INPUTS)assert.equal(parent.sourceHashes?.[relative],pin(path.join(repo,relative)).sha256,'SAME_RUN_PARENT_SOURCE_CHANGED:'+relative);
 const childFile=path.join(parent.childEvidence,'result.json'),binding=parent.rawEvidence?.find(row=>key(row.path)===key(childFile));assert(binding,'SAME_RUN_CURRENT_CHILD_NOT_BOUND');assert.equal(pin(childFile).sha256,binding.sha256,'SAME_RUN_CURRENT_CHILD_CHANGED');
 const child=read(childFile);assert.equal(child.id,'plugins.decky-current-parent-migration');assert.equal(child.sourceUnchanged,true);assert.equal(child.toolsUnchanged,true);
 const sourceMap=new Map(child.sourceInputs.map(row=>[row.path,row.sha256]));for(const relative of OWNED_INPUTS)assert.equal(sourceMap.get(relative),pin(path.join(repo,relative)).sha256,'SAME_RUN_CHILD_SOURCE_CHANGED:'+relative);
 const proof=child.originalProducer;for(const row of Object.values(proof)){assert(within(parent.childWork,row.path),'SAME_RUN_ORIGINAL_OUTSIDE_OWNED_WORK');assert.equal(key(fs.realpathSync(row.path)),key(row.path),'SAME_RUN_ORIGINAL_REPARSE');assert.equal(pin(row.path).sha256,row.sha256,'SAME_RUN_ORIGINAL_CHANGED');}
 const original=path.dirname(proof.raw.path),execution=child.executions.find(row=>row.id==='original-typed');assert(execution,'SAME_RUN_ORIGINAL_PROCESS_MISSING');
 assert.deepEqual(execution.arguments,[path.join(repo,'tools/decky_binding_runtime_selftest.mjs'),original],'SAME_RUN_COMPLETE_COMMAND_REQUIRED');assert.equal(execution.exitCode,0);assert.equal(execution.signal,null);assert.equal(execution.error,null);assert.equal(execution.timeoutMs,135000);assert(execution.elapsedMs<=135000);assert.equal(key(execution.program.path),key(process.execPath));
 for(const row of [execution.program,execution.stdout,execution.stderr])assert.equal(pin(row.path).sha256,row.sha256,'SAME_RUN_PROCESS_EVIDENCE_CHANGED');
 return {original,execution,parent:pin(parentFile),child:pin(childFile)};
}
export async function executeOwnedRuntime({repoRoot=path.resolve(import.meta.dirname,'../../../..'),evidenceDir}){
 const repo=fs.realpathSync(repoRoot),out=path.resolve(evidenceDir);
 assert(!within(repo,out)&&key(repo)!==key(out),'EVIDENCE_OUTSIDE_SOURCE_REQUIRED');
 assert(!within(out,repo),'EVIDENCE_MUST_NOT_CONTAIN_SOURCE');assert(!fs.existsSync(out),'FRESH_EVIDENCE_REQUIRED');
 fs.mkdirSync(out,{recursive:true});assert.equal(key(fs.realpathSync(out)),key(out),'EVIDENCE_REPARSE');
 const report={schemaVersion:1,id:OWNED_ID,status:'FAIL',passed:false,cases:[],sourceHashes:{},sourceInputs:[],toolArtifacts:[],
  fullPersistentAcceptance:false,realSteamOrDeviceActions:0,operatingSystemSleep:false,installedApplicationsStarted:false,
  lifecycleComponent:{featureId:'plugins.decky',persistence:'persistent',role:'current-native-owned-binding-component',parentAdapterId:'plugins.decky-persistent-lifecycle'},
  historicalFailuresRetained:true,boundary:'Original typed Runtime/Client/Owner tests, exact original strict assertions. Not full Decky TS/native lifecycle, hardware, field recovery, package or release acceptance.'};
 const sourcePins=new Map(),toolPins=new Map();
 // Keep the original fixture's Win32 paths short while all new work stays in
 // the project core. Its raw receipts stay at their original live paths.
 const workspace=path.resolve(simulationCorePath("Workspace"));
 assert(fs.statSync(workspace).isDirectory(),'CORE_WORKSPACE_REQUIRED');
 const fixtureRoot=path.join(workspace,'DR');fs.mkdirSync(fixtureRoot,{recursive:true});assert.equal(key(fs.realpathSync(fixtureRoot)),key(fixtureRoot),'SHORT_WORKSPACE_REPARSE');
 const shared=sameRunOriginal(out,repo);const original=shared?.original??path.join(fixtureRoot,crypto.randomUUID().slice(0,8));if(!shared)assert(!fs.existsSync(original));
 report.originalWorkRoot=original;report.originalWorkRootOwner=shared?'same-global-run sibling parent original fixture, full identity reverified':'fresh single original typed-runtime fixture in the existing project core';
 try{
  const declaration=ownedToolDeclaration(repo);assert(!declaration.blockedReason,declaration.blockedReason);
  for(const relative of OWNED_INPUTS){const file=path.join(repo,relative);assert(within(repo,file)&&key(fs.realpathSync(file))===key(file));const row=pin(file);sourcePins.set(relative,row);report.sourceHashes[relative]=row.sha256;}
  const cpp=fs.readFileSync(path.join(repo,'tools/decky_binding_runtime_selftest.cpp'),'utf8');
  assert.deepEqual([...cpp.matchAll(/\btest\("([^"]+)"/g)].map(match=>match[1]).filter(id=>ORIGINAL_REGISTRATION_IDS.includes(id)),ORIGINAL_REGISTRATION_IDS,'ORIGINAL_SOURCE_REGISTRATIONS_CHANGED');
  report.sourceInputs=[...sourcePins].map(([relative,row])=>({path:relative,sha256:row.sha256}));
  for(const file of [...declaration.artifactInputs,process.execPath])toolPins.set(key(file),pin(file));
  report.toolArtifacts=[...toolPins.values()];
  const originalArgs=[path.join(repo,'tools/decky_binding_runtime_selftest.mjs'),original];
  if(shared){
   report.originalExecution={...shared.execution,selection:null,coverage:'complete-current-producer-including-dynamic-registrations',reusedSameRun:true};
   report.sharedExecution={status:'SHARED_ACTUAL_RECEIPT',parent:shared.parent,child:shared.child,businessProcessesStartedByThisComponent:0,originalExecutedInThisGlobalRun:true};
  }else{
   const started=Date.now(),execution=spawnSync(process.execPath,originalArgs,{cwd:out,env:process.env,encoding:'utf8',windowsHide:true,timeout:135000,maxBuffer:8*1024*1024});
   const log=path.join(out,'original.stdout.log');fs.writeFileSync(log,(execution.stdout??'')+(execution.stderr??''));
   report.originalExecution={program:process.execPath,arguments:originalArgs,selection:null,coverage:'complete-current-producer-including-dynamic-registrations',exitCode:execution.status,signal:execution.signal,error:execution.error?.message??null,elapsedMs:Date.now()-started,stdout:pin(log),reusedSameRun:false};
  }
  assert.equal(report.originalExecution.signal,null);assert(!report.originalExecution.error,'ORIGINAL_TYPED_RUNNER_ERROR');assert([0,1].includes(report.originalExecution.exitCode));
  const originalResult=read(path.join(original,'typed-runtime-result.json')),originalRun=read(path.join(original,'RESULT.json'));
  report.summary=validateOriginalResult(originalResult,originalRun);assert.equal(report.summary.completeCurrentProducer,true,'ORIGINAL_FULL_PRODUCER_REQUIRED');
  assert.equal(report.originalExecution.exitCode,originalResult.passed?0:1);
  const frozen=read(path.join(original,'SOURCE-PINS.json'));assert.equal(key(frozen.repo),key(repo));assert.equal(key(frozen.source),key(path.join(original,'source')));
  assert.deepEqual(frozen.files.map(row=>row.path).sort(),[...OWNED_SOURCES].sort(),'ACTUAL_FROZEN_SOURCE_MEMBERSHIP_CHANGED');
  for(const row of frozen.files){assert(!path.isAbsolute(row.path)&&!row.path.split(/[\\/]/).includes('..'));assert.equal(sourcePins.get(row.path)?.sha256,row.sha256);assert.equal(pin(path.join(original,'source',row.path)).sha256,row.sha256);}
  const graph=read(path.join(original,'dependencies.json'));assert.equal(graph.Version,'1.2');
  const frozenFiles=new Map(frozen.files.map(row=>[key(path.join(original,'source',row.path)),row]));
  const readyConsumer=originalReadyConsumer(path.join(original,'source'));frozenFiles.set(key(readyConsumer.path),readyConsumer);report.generatedReadyConsumer=readyConsumer;
  assert.equal(key(graph.Data.Source),key(path.join(original,'source/tools/decky_binding_runtime_selftest.cpp')));
  for(const file of [graph.Data.Source,...graph.Data.Includes]){const frozenRow=frozenFiles.get(key(file));if(frozenRow){assert.equal(pin(file).sha256,frozenRow.sha256);}else {const tool=toolPins.get(key(file));assert(tool,'UNDECLARED_ACTUAL_COMPILER_INPUT:'+file);assert.equal(pin(file).sha256,tool.sha256);}}
  const build=read(path.join(original,'BUILD.json'));assert.equal(build.exit,0);assert.equal(build.error,null);assert.equal(build.compilerSha256,toolPins.get(key(build.compiler))?.sha256);
  const exe=path.join(original,'FixtureSteam/steam.exe'),cef=path.join(original,'FixtureSteam/bin/cef/fixture/steamwebhelper.exe');assert.equal(pin(exe).sha256,originalRun.exeSha256);assert.equal(pin(cef).sha256,originalRun.exeSha256);assert.equal(pin(path.join(original,'binding-client.obj')).sha256,originalRun.objectSha256);
  report.originalResults={raw:pin(path.join(original,'typed-runtime-result.json')),run:pin(path.join(original,'RESULT.json')),frozenSources:pin(path.join(original,'SOURCE-PINS.json')),compilerGraph:pin(path.join(original,'dependencies.json')),build:pin(path.join(original,'BUILD.json')),exe:pin(exe)};
  report.compilerInputs={count:1+graph.Data.Includes.length,allDeclared:true,sourceMembers:frozen.files.length,osLoadedModuleGraph:false};
  report.cases=originalResult.cases;
  report.passed=originalResult.passed;
 }catch(error){report.error=String(error.stack??error);report.passed=false;report.cases.push({id:'owned-runtime-execution-integrity',passed:false,error:String(error.message??error)});}
 const currentHash=file=>{try{return pin(file).sha256;}catch{return null;}};
 for(const [relative,row]of sourcePins){if(currentHash(row.path)!==row.sha256){report.passed=false;(report.sourceMismatches??=[]).push(relative);}}
 for(const row of toolPins.values()){if(currentHash(row.path)!==row.sha256){report.passed=false;(report.toolMismatches??=[]).push(row.path);}}
 report.status=report.passed?'PASS':'FAIL';report.finishedAt=new Date().toISOString();
 const file=path.join(out,'result.json');fs.writeFileSync(file,JSON.stringify(report,null,2)+'\n');
 console.log(JSON.stringify({id:OWNED_ID,status:report.status,summary:report.summary,error:report.error??null,fullPersistentAcceptance:false,out}));return report;
}
if(process.argv[1]&&import.meta.url===pathToFileURL(path.resolve(process.argv[1])).href){const args=process.argv.slice(2),at=args.indexOf('--evidence-dir');assert(at>=0&&args[at+1],'--evidence-dir required');const report=await executeOwnedRuntime({evidenceDir:args[at+1]});process.exitCode=report.passed?0:1;}

