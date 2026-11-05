// Existing complete resource producers; no extra test ID or reduced semantic coverage.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
import {resourceCacheTests} from '../../../decky_resource_cache_selftest.mjs';
import {resourceWorkerTests} from '../../../decky_resource_worker_selftest.mjs';
import {startOwnedRawProducer,rawProgramLauncherDeclaration} from '../decky-persistent-lifecycle/raw-owned-producer.mjs';
import {resourceFixtureEnvironment} from '../decky-persistent-lifecycle/baseline-environment.mjs';
import {getDeckyUpdateRecoveryAdapters} from './adapter.mjs';
import {selectedNativeTools} from '../release-host-environment.mjs';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const packagePin=file=>({path:path.resolve(file),sha256:hash(file),bytes:fs.statSync(file).size});
// Baseline declarations may repeat the same input; RawProgram requires one exact pin per path.
export function resourceDeclaredSourcePins(repo,inputs){
 const result=[],seen=new Map();
 for(const row of inputs){
  if(typeof row.path!=='string'||path.isAbsolute(row.path)||row.path.split(/[\\/]/).includes('..')||!/^[a-f0-9]{64}$/.test(row.sha256))throw Error('Invalid resource source declaration');
  const file=path.resolve(repo,row.path),key=file.toLowerCase();
  if(seen.has(key)){if(seen.get(key)!==row.sha256)throw Error('Conflicting resource source declaration');continue;}
  seen.set(key,row.sha256);result.push({path:file,sha256:row.sha256});
 }
 return result;
}
// Both resources spend the original baseline's remainder, including preparation and cleanup.
export function allocateResourceBudget(deadlineEpochMs,allocatedAtEpochMs=Date.now()){
 const parentBudgetMs=240000,originalBudgetMs=240000,cleanupReserveMs=5000;
 const parentStartedAtEpochMs=deadlineEpochMs-parentBudgetMs,parentRemainingBeforeMs=deadlineEpochMs-allocatedAtEpochMs;
 if(![deadlineEpochMs,allocatedAtEpochMs,parentStartedAtEpochMs].every(value=>Number.isSafeInteger(value)&&value>0)||allocatedAtEpochMs<parentStartedAtEpochMs)throw Error('Invalid original resource execution contract');
 if(parentRemainingBeforeMs<=cleanupReserveMs)throw Error('Original baseline resource budget exhausted');
 return {allocatedAtEpochMs,parentStartedAtEpochMs,parentBudgetMs,originalBudgetMs,parentRemainingBeforeMs,allocatedBudgetMs:Math.min(originalBudgetMs,parentRemainingBeforeMs),deadlineEpochMs,cleanupReserveMs};
}
// The explicit sequential option exists for paired cost measurement of these same producers.
export async function executeBaselineResources({repo:root,out,sourceInputs:inputs,deadlineEpochMs,mode='auto',mutations=true}){
 if(typeof mutations!=='boolean'||!['auto','owned-sequential'].includes(mode)||!Number.isFinite(deadlineEpochMs)||deadlineEpochMs>Date.now()+240000)throw Error('Invalid original resource execution contract');
 const resourceStarted=performance.now(),report={resourceScheduling:{originalBaselineBudgetMs:240000,parentStartedAtEpochMs:deadlineEpochMs-240000,parentBudgetMs:240000,deadlineEpochMs,allocations:{}}},resourceOwnedProducers=[];let executionError=null;
 try{
 // Bind the original complete in-process producer to this exact baseline child.
 report.resourceExecutions={};
 const resourceLauncher=rawProgramLauncherDeclaration();
 if(mode==='owned-sequential'&&!resourceLauncher)throw Error('Paired sequential measurement requires the same owned launcher');
 let resourceRuntime=null;
 if(resourceLauncher){
  const declaration=getDeckyUpdateRecoveryAdapters(root)[0];
  if(declaration.blockedReason)throw Error(declaration.blockedReason);
  const tools=declaration.artifactInputs.map(packagePin),host=path.join(root,'tools/ymcc_sim/global/host-process.ps1');
  const owner=spawnSync(resourceLauncher.program,['-NoLogo','-NoProfile','-NonInteractive','-Command','([Diagnostics.Process]::GetProcessById('+process.pid+')).StartTime.ToUniversalTime().Ticks.ToString()'],{cwd:root,encoding:'utf8',windowsHide:true,timeout:Math.min(5000,Math.max(1,deadlineEpochMs-Date.now()))});
  if(owner.status!==0||!/^\d{17,20}$/.test(owner.stdout.trim()))throw Error('Resource owned producer parent identity unavailable');
  const telemetryPath=path.join(path.dirname(selectedNativeTools().native.compiler),'vctip.exe'),telemetry=tools.find(row=>path.resolve(row.path).toLowerCase()===path.resolve(telemetryPath).toLowerCase());
  if(!telemetry)throw Error('Resource compiler telemetry is not predeclared');
  resourceRuntime={tools,host,ownerStartTicks:owner.stdout.trim(),sources:resourceDeclaredSourcePins(root,inputs),toolCleanupPolicy:{kind:'MSVC_VCTIP_AFTER_ROOT_EXIT',maximumProcesses:1,program:telemetry}};
 }
 const executeOwnedResource=async(family,module,folder)=>{
  const allocation=allocateResourceBudget(deadlineEpochMs);
  report.resourceScheduling.allocations[family]=allocation;
  const producer=startOwnedRawProducer({id:'resource-'+family,program:process.execPath,args:[path.join(root,module),folder,...(mutations?[]:['--positive-only'])],cwd:root,env:process.env,host:resourceRuntime.host,powershell:resourceLauncher.program,ownerStartTicks:resourceRuntime.ownerStartTicks,directory:path.join(out,'owned-resource-'+family),stdoutPath:path.join(out,'resource-'+family+'.stdout.bin'),stderrPath:path.join(out,'resource-'+family+'.stderr.bin'),...allocation,sourceInputs:resourceRuntime.sources,toolArtifacts:resourceRuntime.tools,toolCleanupPolicy:resourceRuntime.toolCleanupPolicy});
  resourceOwnedProducers.push({family,producer});
  const proof=await producer.wait(),proofFile=path.join(out,'resource-'+family+'-process.json');
  fs.writeFileSync(proofFile,JSON.stringify(proof,null,2)+'\n',{flag:'wx'});
  if(proof.error!==null||proof.exitCode!==0||proof.timedOut||proof.outputTruncated||proof.rawExecution?.collectionComplete!==true)throw Error('Owned resource '+family+' failed: '+JSON.stringify({error:proof.error,exitCode:proof.exitCode,timedOut:proof.timedOut}));
  return {value:JSON.parse(fs.readFileSync(path.join(folder,'result.json'),'utf8')),ownedProducer:{schemaVersion:1,mode:mutations?'complete-original-cli':'positive-only-cost-measurement',process:packagePin(proofFile)}};
 };
 const captureResource=async(family,module,exportName,producer)=>{
  const folder=path.join(out,'resource-'+family),moduleFile=path.join(root,module),moduleBefore=packagePin(moduleFile),environment=resourceFixtureEnvironment(),startedAt=new Date().toISOString(),timer=performance.now();
  const execution=resourceRuntime?await executeOwnedResource(family,module,folder):null;
  const value=execution?execution.value:await producer(root,folder,{mutations});
  const moduleAfter=packagePin(moduleFile);if(moduleAfter.sha256!==moduleBefore.sha256)throw Error('Resource producer module changed: '+family);
  if(JSON.stringify(resourceFixtureEnvironment())!==JSON.stringify(environment))throw Error('Resource producer environment changed: '+family);
  const names=['positive',...value.negativeMutations.map(row=>'negative-'+row.name)];
  const files=Object.fromEntries(Object.entries({result:'result.json',summary:'EXECUTION-SUMMARY.json',workspace:'WORKSPACE.json',freeze:'SOURCE-FREEZE.json',audit:'SOURCE-AUDIT.json',tools:'TOOL-GRAPH.json'}).map(([name,file])=>[name,packagePin(path.join(folder,file))]));
  const runs=names.map(name=>{const directory=path.join(folder,name);return {name,execution:packagePin(path.join(directory,'EXECUTION.json')),native:packagePin(path.join(directory,'native.json')),paths:packagePin(path.join(directory,'PATHS.json')),buildProcess:packagePin(path.join(directory,'build.PROCESS.json')),runProcess:packagePin(path.join(directory,'run.PROCESS.json')),dependencies:packagePin(path.join(directory,'dependencies.json'))};});
  report.resourceExecutions[family]={schemaVersion:1,family,exportName,options:{mutations},module:moduleBefore,moduleSha256After:moduleAfter.sha256,caller:{pid:process.pid,node:packagePin(process.execPath),cwd:process.cwd(),arguments:process.argv.slice(1)},environment,startedAt,finishedAt:new Date().toISOString(),elapsedMs:performance.now()-timer,files,runs,mutations:value.negativeMutations.map(row=>({name:row.name,proof:packagePin(path.join(folder,'mutation-'+row.name+'.json'))}))};
  if(execution)report.resourceExecutions[family].ownedProducer=execution.ownedProducer;
  return value;
 };
 if(resourceRuntime&&mode!=='owned-sequential'){
  const executions=await Promise.allSettled([captureResource('cache','tools/decky_resource_cache_selftest.mjs','resourceCacheTests',resourceCacheTests),captureResource('worker','tools/decky_resource_worker_selftest.mjs','resourceWorkerTests',resourceWorkerTests)]);
  const failure=executions.find(row=>row.status==='rejected');
  report.resourceScheduling={...report.resourceScheduling,mode:'owned-parallel',elapsedMs:performance.now()-resourceStarted,originalBaselineBudgetMs:240000,producerCount:2,joinedProducers:executions.length,mutations,coverageChanged:false};
  if(failure)throw failure.reason;
  [report.resourceCache,report.resourceWorker]=executions.map(row=>row.value);
 }else{
  report.resourceCache=await captureResource('cache','tools/decky_resource_cache_selftest.mjs','resourceCacheTests',resourceCacheTests);
  report.resourceWorker=await captureResource('worker','tools/decky_resource_worker_selftest.mjs','resourceWorkerTests',resourceWorkerTests);
  report.resourceScheduling={...report.resourceScheduling,mode:resourceRuntime?'owned-sequential':'original-sequential',elapsedMs:performance.now()-resourceStarted,originalBaselineBudgetMs:240000,producerCount:2,mutations,coverageChanged:false};
 }
 return report;
 }catch(error){executionError=error;throw error;}
 finally{
  const cleanupErrors=[];
  for(const {family,producer}of resourceOwnedProducers){try{const proof=await producer.cancel();if(proof.rawExecution?.collectionComplete!==true||proof.rawExecution?.job?.disposed!==true)throw Error('Owned resource '+family+' collection incomplete');}catch(error){cleanupErrors.push(error);}}
  if(executionError||cleanupErrors.length)fs.writeFileSync(path.join(out,'resource-execution-failure.json'),JSON.stringify({status:'FAIL',error:String(executionError?.stack??executionError??''),cleanupErrors:cleanupErrors.map(error=>String(error.stack??error)),resourceExecutions:report.resourceExecutions,resourceScheduling:report.resourceScheduling},null,2)+'\n');
  if(cleanupErrors.length)throw new AggregateError([...(executionError?[executionError]:[]),...cleanupErrors],'Owned resource retirement not proven');
 }
}
