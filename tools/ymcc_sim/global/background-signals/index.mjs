import {promises as fs} from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {createHash,randomUUID} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {getBackgroundAdapters} from './adapter.mjs';
export {getBackgroundAdapters};
const here=path.dirname(fileURLToPath(import.meta.url));
const defaultRepo=path.resolve(here,'../../../..');
const defaultPwsh='C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe';
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const json=async file=>JSON.parse((await fs.readFile(file,'utf8')).replace(/^\uFEFF/,''));
const failure=code=>Object.assign(new Error(code),{code});
const requiredIds=['policy.hysteresis','policy.invalid-samples','policy.video-reconciliation','bridge.fixed-and-dynamic-requests','bridge.visual-settings-feedback','bridge.settings-io-failure','bridge.native-io-failure','dynamic.cached-media','dynamic.disabled-no-io','dynamic.duplicate-inflight','dynamic.pid-reuse','dynamic.missing-process-identity','dynamic.disabled-during-request','dynamic.read-disabled-during-request','dynamic.deleted-entry','dynamic.invalid-entry','dynamic.revision-invalidated','dynamic.native-io-failure','dynamic.missing-ack','dynamic.pending-without-ack'];
function within(root,target){const relative=path.relative(root,target);return !relative||!relative.startsWith('..')&&!path.isAbsolute(relative);}
export async function snapshotBackgroundInputs(repoRoot,files){return Promise.all(files.map(async file=>{const absolute=path.resolve(repoRoot,file);if(!within(repoRoot,absolute))throw failure('SOURCE_INPUT_OUTSIDE_REPOSITORY');try{return {path:file,sha256:sha(await fs.readFile(absolute)),status:'OK'};}catch(error){return {path:file,sha256:null,status:'MISSING',code:error.code};}}));}
export async function verifyBackgroundInputs(repoRoot,before){const after=await snapshotBackgroundInputs(repoRoot,before.map(item=>item.path));return {status:after.every((item,index)=>item.status==='OK'&&item.sha256===before[index].sha256)?'PASS':'FAIL',changed:after.filter((item,index)=>item.status!=='OK'||item.sha256!==before[index].sha256)};}
export function validateBackgroundReceipt(worker){
 const issues=[];
 if(worker?.schemaVersion!==1||worker?.status!=='PASS')issues.push('WORKER_NOT_PASS');
 if(worker?.compatibility?.schemaVersion!==1||worker.compatibility.status!=='PASS'||worker.compatibility.hcRuntimeRequired!==false||worker.compatibility.requiredExports?.length!==16)issues.push('BACKGROUND_INTERFACE_CONTRACT_MISSING');
 if(!Array.isArray(worker?.checks)||worker.checks.length!==requiredIds.length||new Set(worker.checks.map(item=>item.id)).size!==requiredIds.length||requiredIds.some(id=>!worker?.checks?.some(item=>item.id===id&&item.status==='PASS'&&item.inputConsumerStateFeedback?.source==='source-derived-fixture'&&item.inputConsumerStateFeedback.feedback)))issues.push('BEHAVIOR_RECEIPTS_INCOMPLETE');
 if(worker?.originalDynamic?.status!=='PASS'||worker?.originalNative?.status!=='PASS'||worker.originalNative.checks<12)issues.push('ORIGINAL_TEST_RECEIPTS_MISSING');
 const compiled=Array.isArray(worker?.compiledInputs)?worker.compiledInputs:[];
 if(!compiled.length||compiled.some(item=>typeof item.path!=='string'||!/^[0-9a-f]{64}$/.test(item.sha256)))issues.push('ACTUAL_COMPILED_INPUT_HASHES_MISSING');
 for(const module of ['src/bridge/background.ts','src/bridge/backgroundPolicy.ts','src/bridge/dynamicBackground.ts','src/bridge/sharedGameMedia.ts','src/bridge/gameMediaRevisionFence.ts','src/bridge/steamMediaSource.ts'])
  if(!worker?.executedProductModules?.includes(module)||!compiled.some(item=>item.path===module))issues.push('FORMAL_PRODUCT_MODULE_NOT_EXECUTED:'+module);
 const safety=worker?.behavior?.receipt?.safety;
 if(worker?.behavior?.exitCode!==0||worker?.behavior?.timedOut||!safety||safety.realNativeCalls!==0||safety.userStorageReads!==0||safety.wallpaperChanged!==false||safety.hostStarted!==false||safety.appStarted!==false||safety.subscriptions!==0||safety.forbiddenCalls?.length!==0)issues.push('BOUNDARY_OR_PROCESS_RECEIPT_INVALID');
 const rows=Array.isArray(worker?.checks)?worker.checks:[];
 const loadedCount=id=>rows.find(item=>item.id===id)?.inputConsumerStateFeedback?.feedback?.events?.filter(item=>item.name==='dynamic-background:loaded').length;
 for(const id of ['dynamic.cached-media','dynamic.duplicate-inflight'])if(loadedCount(id)!==1)issues.push('EXPECTED_DYNAMIC_EVENT_RECEIPT_MISSING:'+id);
 for(const id of ['dynamic.disabled-no-io','dynamic.pid-reuse','dynamic.missing-process-identity','dynamic.disabled-during-request','dynamic.read-disabled-during-request','dynamic.deleted-entry','dynamic.invalid-entry','dynamic.revision-invalidated','dynamic.native-io-failure','dynamic.missing-ack','dynamic.pending-without-ack'])if(loadedCount(id)!==0)issues.push('UNEXPECTED_DYNAMIC_SUCCESS_EVENT:'+id);
 if(!Array.isArray(worker?.nativeSourceInputs)||!worker.nativeSourceInputs.some(item=>item.path==='native/main.cpp'&&/^[0-9a-f]{64}$/.test(item.sha256))||!worker?.nativeArtifacts?.some(item=>item.path==='native/fixture.exe')||worker?.executedNativeRegions?.length!==2)issues.push('ACTUAL_NATIVE_SOURCE_OR_ARTIFACT_HASHES_MISSING');
 if(worker?.changedCompiledInputs?.length)issues.push('COMPILED_INPUTS_CHANGED');
 return {status:issues.length?'FAIL':'PASS',issues};
}
async function executeOwned({pwshPath,nodePath,repoRoot,evidenceDir,timeoutMs}){
 const argumentsPath=path.join(evidenceDir,'owned-arguments.json');
 await fs.writeFile(argumentsPath,JSON.stringify(['-NodePath',nodePath,'-RepoRoot',repoRoot,'-EvidenceDir',evidenceDir]));
 return new Promise(resolve=>{
  const process=spawn(pwshPath,['-NoLogo','-NoProfile','-NonInteractive','-File',path.join(repoRoot,'tools/ymcc_sim/global/host-process.ps1'),'-ScriptPath',path.join(here,'Run-Background.ps1'),'-ArgumentsPath',argumentsPath],{windowsHide:true,stdio:['ignore','pipe','pipe']});
  const result={pid:process.pid??null,spawnedAt:new Date().toISOString(),executable:pwshPath,exitCode:null,timedOut:false,launchError:null,outputTruncated:false,cleanup:'owned process handle and gated kill-on-close Windows Job descendants'};
  let stdout='',stderr='';const collect=(current,chunk)=>{if(current.length+chunk.length>262144){result.outputTruncated=true;return current;}return current+chunk.toString('utf8');};
  process.stdout.on('data',chunk=>{stdout=collect(stdout,chunk);});process.stderr.on('data',chunk=>{stderr=collect(stderr,chunk);});
  const timer=setTimeout(()=>{result.timedOut=true;process.kill();},timeoutMs);
  process.on('error',error=>{result.launchError=error.code??error.name;});
  process.on('close',async(code,signal)=>{clearTimeout(timer);result.exitCode=code;result.signal=signal;try{await Promise.all([fs.writeFile(path.join(evidenceDir,'runner.stdout.log'),stdout),fs.writeFile(path.join(evidenceDir,'runner.stderr.log'),stderr)]);}catch(error){result.launchError??=error.code??'EVIDENCE_WRITE_FAILED';}resolve(result);});
 });
}
export async function runBackgroundRegressions(input={}){
 const repoRoot=path.resolve(input.repoRoot??defaultRepo);if(!input.evidenceDir)throw failure('FRESH_EVIDENCE_DIRECTORY_REQUIRED');
 const evidenceDir=path.resolve(input.evidenceDir);if(within(repoRoot,evidenceDir))throw failure('EVIDENCE_MUST_BE_OUTSIDE_REPOSITORY');
 const timeoutMs=input.timeoutMs??90000;if(!Number.isInteger(timeoutMs)||timeoutMs<500||timeoutMs>300000)throw failure('BOUNDED_TIMEOUT_REQUIRED');
 await fs.mkdir(evidenceDir,{recursive:false});
 const result={schemaVersion:1,id:'display.background-signals',runId:randomUUID(),status:'BLOCKED',startedAt:new Date().toISOString(),sourceChanges:['src/bridge/backgroundPolicy.ts','src/bridge/dynamicBackground.ts'],sourceHashes:[],actualCompiledInputs:[],executedProductModules:[],inputConsumerStateFeedback:[],checks:[],remainingGaps:[],claims:{physicalHardwareValidated:false,operatingSystemSleepPerformed:false,installedApplicationExercised:false,desktopWallpaperChanged:false,hostStarted:false,userConfigurationRead:false}};
 try{
  const inputs=getBackgroundAdapters()[0].evidenceInputs;
  const before=await snapshotBackgroundInputs(repoRoot,inputs);result.sourceHashes=before;
  if(before.some(item=>item.status!=='OK'))throw failure('REQUIRED_PRODUCT_OR_TOOL_INPUT_MISSING');
  result.process=await executeOwned({pwshPath:input.pwshPath??defaultPwsh,nodePath:process.execPath,repoRoot,evidenceDir,timeoutMs});
  if(result.process.timedOut)throw failure('BACKGROUND_RUN_TIMEOUT');
  if(result.process.launchError)throw failure('OWNED_JOB_OR_PS7_UNAVAILABLE');
  if(result.process.outputTruncated)throw failure('BACKGROUND_OUTPUT_TRUNCATED');
  let worker;try{worker=await json(path.join(evidenceDir,'worker-result.json'));}catch{throw failure('BACKGROUND_WORKER_RECEIPT_MISSING');}
  result.status='FAIL';result.validation=validateBackgroundReceipt(worker);
  result.checks=worker.checks??[];result.inputConsumerStateFeedback=worker.inputConsumerStateFeedback??[];
  result.executedProductModules=worker.executedProductModules??[];result.actualCompiledInputs=worker.compiledInputs??[];
  result.compatibility=worker.compatibility;result.originalDynamic=worker.originalDynamic;result.originalNative=worker.originalNative;result.bundles=worker.bundles;result.nativeToolchain=worker.nativeToolchain;
  result.nativeSourceInputs=worker.nativeSourceInputs;result.nativeArtifacts=worker.nativeArtifacts;result.executedNativeRegions=worker.executedNativeRegions;result.nativeTestAdaptation=worker.nativeTestAdaptation;result.remainingGaps=worker.remainingGaps??[];result.failure=worker.failure??null;
  const combined=new Map(before.map(item=>[item.path,item]));
  for(const item of [...result.actualCompiledInputs,...(worker.nativeSourceInputs??[])]){const old=combined.get(item.path);if(old&&old.sha256!==item.sha256)throw failure('ACTUAL_COMPILE_INPUT_DIFFERS_FROM_RUN_SNAPSHOT');combined.set(item.path,{...item,status:'OK'});}
  result.sourceHashes=[...combined.values()].sort((a,b)=>a.path.localeCompare(b.path));
  result.sourceVerification=await verifyBackgroundInputs(repoRoot,result.sourceHashes);
  if(result.process.exitCode===0&&result.validation.status==='PASS'&&result.sourceVerification.status==='PASS')result.status='PASS';
  if(result.sourceVerification.status!=='PASS')result.failure={code:'SOURCE_CHANGED_DURING_BACKGROUND_RUN',changed:result.sourceVerification.changed};
 }catch(error){result.status=error.code?.includes('CHANGED')||error.code?.includes('DIFFERS')?'FAIL':'BLOCKED';result.failure={code:error.code??error.message,exceptionType:error.name};result.remainingGaps.push('Required actual compiler/source/transport/receipt was unavailable; no mock-success fallback.');}
 result.finishedAt=new Date().toISOString();await fs.writeFile(path.join(evidenceDir,'result.json'),JSON.stringify(result,null,2));return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const mapping={'--repo-root':'repoRoot','--evidence-dir':'evidenceDir','--timeout-ms':'timeoutMs','--pwsh-path':'pwshPath'},args={};for(let index=2;index<process.argv.length;index+=2){const key=mapping[process.argv[index]];if(!key||process.argv[index+1]===undefined)throw failure('UNKNOWN_OR_MISSING_ARGUMENT');args[key]=key==='timeoutMs'?Number(process.argv[index+1]):process.argv[index+1];}
  const result=await runBackgroundRegressions(args);console.log(JSON.stringify({status:result.status,checks:result.checks.length,nativeChecks:result.originalNative?.checks??0,failure:result.failure??null}));process.exitCode=result.status==='PASS'?0:result.status==='FAIL'?1:2;
 }catch(error){console.error(JSON.stringify({status:'BLOCKED',code:error.code??error.name}));process.exitCode=2;}
}



