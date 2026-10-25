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
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const pin=file=>({path:file,sha256:sha(fs.readFileSync(file))});
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const key=file=>path.resolve(file).toLowerCase();
const within=(root,file)=>key(file).startsWith(key(root)+path.sep);
export function validateOriginalResult(value,run){
 assert.equal(value.schema,'ymcc.typed-binding-runtime-gate.v1');
 assert.equal(value.tests,ORIGINAL_CASES.length);assert.equal(value.deadlineMs,3200);
 assert.equal(value.productCallbackMode,'typed-only; legacy macro absent');
 assert.equal(value.realSteamOrDeviceActions,0);
 assert.equal(value.originalHistoricalFailurePreserved,true);
 assert.equal(value.fieldWakeOrS4Verified,false);assert.equal(value.hostPackagePassed,false);
 assert.equal(value.cases.length,ORIGINAL_CASES.length,'ORIGINAL_TYPED_CASE_COUNT');
 for(let i=0;i<ORIGINAL_CASES.length;i++){const row=value.cases[i];assert(row.id===ORIGINAL_IDS[i]||(row.passed===false&&typeof row.error==='string'&&row.error.length>0&&row.id===ORIGINAL_REGISTRATION_IDS[i]),'ORIGINAL_TYPED_CASES_CHANGED');}
 assert(value.cases.every(row=>typeof row.passed==='boolean'),'TYPED_CASE_VERDICT_REQUIRED');
 const passed=value.cases.filter(row=>row.passed).length;
 assert.equal(value.passedCount,passed);assert.equal(value.passed,passed===ORIGINAL_CASES.length);
 assert.equal(run.runExit,value.passed?0:1);assert.equal(run.error,null);
 assert.equal(run.compilerExit,0);assert.deepEqual(run.changedOriginalInputs,[]);
 assert.equal(run.status,value.passed?'pass':'fail');
 return {cases:ORIGINAL_CASES.length,passed,failed:ORIGINAL_CASES.length-passed,failedCaseIds:value.cases.filter(row=>!row.passed).map(row=>row.id)};
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
 const workspace=path.resolve('G:/YeManCC-Work/Testing/YMCC/Workspace');
 assert(fs.statSync(workspace).isDirectory(),'CORE_WORKSPACE_REQUIRED');
 const fixtureRoot=path.join(workspace,'DR');fs.mkdirSync(fixtureRoot,{recursive:true});assert.equal(key(fs.realpathSync(fixtureRoot)),key(fixtureRoot),'SHORT_WORKSPACE_REPARSE');
 const original=path.join(fixtureRoot,crypto.randomUUID().slice(0,8));assert(!fs.existsSync(original));
 report.originalWorkRoot=original;report.originalWorkRootOwner='fresh single original typed-runtime fixture in the existing project core';
 try{
  const declaration=ownedToolDeclaration(repo);assert(!declaration.blockedReason,declaration.blockedReason);
  for(const relative of OWNED_INPUTS){const file=path.join(repo,relative);assert(within(repo,file)&&key(fs.realpathSync(file))===key(file));const row=pin(file);sourcePins.set(relative,row);report.sourceHashes[relative]=row.sha256;}
  const cpp=fs.readFileSync(path.join(repo,'tools/decky_binding_runtime_selftest.cpp'),'utf8');
  assert.deepEqual([...cpp.matchAll(/\btest\("([^"]+)"/g)].map(match=>match[1]),ORIGINAL_REGISTRATION_IDS,'ORIGINAL_SOURCE_REGISTRATIONS_CHANGED');
  report.sourceInputs=[...sourcePins].map(([relative,row])=>({path:relative,sha256:row.sha256}));
  for(const file of [...declaration.artifactInputs,process.execPath])toolPins.set(key(file),pin(file));
  report.toolArtifacts=[...toolPins.values()];
  const started=Date.now(),execution=spawnSync(process.execPath,[path.join(repo,'tools/decky_binding_runtime_selftest.mjs'),original],{cwd:out,env:process.env,encoding:'utf8',windowsHide:true,timeout:135000,maxBuffer:8*1024*1024});
  const log=path.join(out,'original.stdout.log');fs.writeFileSync(log,(execution.stdout??'')+(execution.stderr??''));
  report.originalExecution={program:process.execPath,arguments:[path.join(repo,'tools/decky_binding_runtime_selftest.mjs'),original],exitCode:execution.status,signal:execution.signal,error:execution.error?.message??null,elapsedMs:Date.now()-started,stdout:pin(log)};
  assert.equal(execution.signal,null);assert(!execution.error,'ORIGINAL_TYPED_RUNNER_ERROR');assert([0,1].includes(execution.status));
  const originalResult=read(path.join(original,'typed-runtime-result.json')),originalRun=read(path.join(original,'RESULT.json'));
  report.summary=validateOriginalResult(originalResult,originalRun);
  assert.equal(execution.status,originalResult.passed?0:1);
  const frozen=read(path.join(original,'SOURCE-PINS.json'));assert.equal(key(frozen.repo),key(repo));assert.equal(key(frozen.source),key(path.join(original,'source')));
  assert.deepEqual(frozen.files.map(row=>row.path).sort(),[...OWNED_SOURCES].sort(),'ACTUAL_FROZEN_SOURCE_MEMBERSHIP_CHANGED');
  for(const row of frozen.files){assert(!path.isAbsolute(row.path)&&!row.path.split(/[\\/]/).includes('..'));assert.equal(sourcePins.get(row.path)?.sha256,row.sha256);assert.equal(pin(path.join(original,'source',row.path)).sha256,row.sha256);}
  const graph=read(path.join(original,'dependencies.json'));assert.equal(graph.Version,'1.2');
  const frozenFiles=new Map(frozen.files.map(row=>[key(path.join(original,'source',row.path)),row]));
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

