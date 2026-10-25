import {lockBundledNodeTools,verifyUpdateToolEvidence,ownedUpdateOutputs} from './tool-scope.mjs';
import {promises as fs} from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {createHash,randomUUID} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {getUpdateDownloadAdapters} from './adapter.mjs';
export {getUpdateDownloadAdapters};
const here=path.dirname(fileURLToPath(import.meta.url)),defaultRepo=path.resolve(here,'../../../..');
const defaultPwsh='C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe';
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const json=async file=>JSON.parse((await fs.readFile(file,'utf8')).replace(/^\uFEFF/,''));
const failure=code=>Object.assign(new Error(code),{code});
const within=(root,target)=>{const relative=path.relative(root,target);return !relative||!relative.startsWith('..')&&!path.isAbsolute(relative);};
const requiredIds=['scheduler.ready-once','scheduler.failure-backoff','scheduler.offline-and-resume','scheduler.concurrent-dedup','scheduler.disable-late-check','scheduler.stop-late-refresh','scheduler.initialize-retry','scheduler.bad-policy','scheduler.cancel-during-notice','scheduler.target-changed-in-notice','scheduler.fractional-backoff-policy','scheduler.schedule-write-failure','scheduler.pause-not-failure','provider.fresh-subscription-beats-old-read-error','provider.invalid-receipt','provider.replacement-late-read','provider.write-failure-feedback','service.rejected-activity-ack','service.idle-resync-preserves-transfer','manager.unknown-post-owner','manager.cancel-timeout-no-replay','manager.native-proof-recovers-unknown-post','manager.export-version-fence'];
const requiredStability={'missing-start':2,'active-stall':2,'transport-reset':2,'slow-progress':1,'cleanup-running':2,'cleanup-probe':2,'cleanup-no-proof':1,'cleanup-error':2,'cleanup-busy':2,'cleanup-idle':2,'late-check':1,'late-initialize':1,'manual-protection':3,'scheduler-quiet':2,'game-recovery':10};
const requiredOriginals={'scheduler-original':30,'integration-original':8,'service-original':8,'successor-original':11};
export async function snapshotUpdateDownloadInputs(repoRoot,files){return Promise.all(files.map(async file=>{const absolute=path.resolve(repoRoot,file);if(!within(repoRoot,absolute))throw failure('SOURCE_INPUT_OUTSIDE_REPOSITORY');try{return {path:file,sha256:sha(await fs.readFile(absolute)),status:'OK'};}catch(error){return {path:file,sha256:null,status:'MISSING',code:error.code};}}));}
export async function verifyUpdateDownloadInputs(repoRoot,before){const after=await snapshotUpdateDownloadInputs(repoRoot,before.map(item=>item.path));const changed=after.filter((item,index)=>item.status!=='OK'||item.sha256!==before[index].sha256);return {status:changed.length?'FAIL':'PASS',changed};}
export function validateUpdateDownloadReceipt(worker){
 const issues=[],rows=Array.isArray(worker?.checks)?worker.checks:[],compiled=Array.isArray(worker?.compiledInputs)?worker.compiledInputs:[];
 if(worker?.schemaVersion!==1||worker?.status!=='PASS')issues.push('WORKER_NOT_PASS');
 if(rows.length!==requiredIds.length||new Set(rows.map(item=>item.id)).size!==requiredIds.length||requiredIds.some(id=>!rows.some(item=>item.id===id&&item.status==='PASS'&&item.inputConsumerStateFeedback?.source==='source-derived-fixture'&&item.inputConsumerStateFeedback.feedback)))issues.push('BEHAVIOR_FEEDBACK_INCOMPLETE');
 for(const[id,count]of Object.entries(requiredOriginals))if(!worker?.originalTests?.some(item=>item.id===id&&item.status==='PASS'&&item.checks===count&&item.exitCode===0&&!item.errorCode))issues.push('ORIGINAL_ASSERTIONS_MISSING:'+id);
 for(const module of worker?.compatibility?.requiredModules??[])if(!worker?.executedProductModules?.includes(module)||!compiled.some(item=>item.path===module&&/^[0-9a-f]{64}$/.test(item.sha256)))issues.push('PRODUCT_MODULE_NOT_COMPILED_AND_EXECUTED:'+module);
 if(worker?.compatibility?.schemaVersion!==1||worker.compatibility.hcRuntimeRequired!==false||worker.compatibility.requiredModules?.length!==7)issues.push('COMPATIBILITY_CONTRACT_MISSING');
 if(!compiled.length||compiled.some(item=>!/^[0-9a-f]{64}$/.test(item.sha256)))issues.push('EXACT_COMPILED_INPUTS_MISSING');
 const stability=worker?.stabilityTests??[];
 if(stability.length!==15||new Set(stability.map(item=>item.id)).size!==15||Object.entries(requiredStability).some(([id,count])=>!stability.some(item=>item.id===id&&item.status==='PASS'&&item.exitCode===0&&item.receipt?.status==='PASS'&&item.receipt.checks===count&&item.receipt.records?.length===count)))issues.push('STABILITY_OR_GAME_RECOVERY_RECEIPT_MISSING');
 for(const module of ['src/bridge/gamePolicyTarget.ts','src/bridge/gamePolicyHysteresis.ts'])
  if(!worker?.executedProductModules?.includes(module)||!compiled.some(item=>item.path===module&&/^[0-9a-f]{64}$/.test(item.sha256)))issues.push('ACTUAL_GAME_POLICY_NOT_EXECUTED:'+module);
 const one=id=>rows.find(item=>item.id===id)?.inputConsumerStateFeedback;
 const unknown=one('manager.unknown-post-owner'),cancel=one('manager.cancel-timeout-no-replay'),proof=one('manager.native-proof-recovers-unknown-post'),activity=one('service.rejected-activity-ack');
 for(const item of [unknown,cancel,proof])if(item?.state?.downloadRequests!==1||item?.feedback?.calls?.filter(call=>call.cmd==='app.downloadUpdate').length!==1)issues.push('UNKNOWN_POST_REPLAY_OR_RECEIPT_MISSING');
 if(unknown?.state?.settled!==false||proof?.state?.snapshot?.phase!=='downloaded'||proof?.state?.installed!==false)issues.push('NATIVE_PROOF_OR_PENDING_STATE_MISSING');
 if(activity?.state?.networkRequests!==0||activity?.feedback?.calls?.some(call=>['app.checkUpdate','app.downloadUpdate','app.installUpdate'].includes(call.cmd)))issues.push('REJECTED_ACTIVITY_FALSE_SUCCESS');
 if(rows.some(item=>item.inputConsumerStateFeedback?.feedback?.calls?.some(call=>call.cmd==='app.installUpdate')))issues.push('AUTO_INSTALL_COMMAND_FORBIDDEN');
 if(!Array.isArray(worker?.ioGuards)||worker.ioGuards.length!==11||worker.ioGuards.some(item=>item.fetch!==0||item.userStorage!==0))issues.push('FORBIDDEN_IO_OR_GUARD_RECEIPT_MISSING');
 if(worker?.changedCompiledInputs?.length)issues.push('COMPILED_INPUTS_CHANGED');
 return {status:issues.length?'FAIL':'PASS',issues};
}
async function executeOwned({pwshPath,nodePath,repoRoot,evidenceDir,timeoutMs}){
 const argumentsPath=path.join(evidenceDir,'owned-arguments.json');await fs.writeFile(argumentsPath,JSON.stringify(['-NodePath',nodePath,'-RepoRoot',repoRoot,'-EvidenceDir',evidenceDir]));
 return new Promise(resolve=>{const child=spawn(pwshPath,['-NoLogo','-NoProfile','-NonInteractive','-File',path.join(repoRoot,'tools/ymcc_sim/global/host-process.ps1'),'-ScriptPath',path.join(here,'Run-UpdateDownload.ps1'),'-ArgumentsPath',argumentsPath],{windowsHide:true,stdio:['ignore','pipe','pipe']});
  const result={pid:child.pid??null,spawnedAt:new Date().toISOString(),executable:pwshPath,exitCode:null,timedOut:false,launchError:null,outputTruncated:false,cleanup:'owned child handle; gated Windows Job kill-on-close descendants'};let stdout='',stderr='';
  const collect=(current,chunk)=>{if(current.length+chunk.length>262144){result.outputTruncated=true;return current;}return current+chunk.toString('utf8');};child.stdout.on('data',chunk=>{stdout=collect(stdout,chunk);});child.stderr.on('data',chunk=>{stderr=collect(stderr,chunk);});
  const timer=setTimeout(()=>{result.timedOut=true;child.kill();},timeoutMs);child.on('error',error=>{result.launchError=error.code??error.name;});child.on('close',async(code,signal)=>{clearTimeout(timer);result.exitCode=code;result.signal=signal;try{await Promise.all([fs.writeFile(path.join(evidenceDir,'runner.stdout.log'),stdout),fs.writeFile(path.join(evidenceDir,'runner.stderr.log'),stderr)]);}catch(error){result.launchError??=error.code??'EVIDENCE_WRITE_FAILED';}resolve(result);});
 });
}
export async function runUpdateDownloadRegressions(input={}){
 const repoRoot=path.resolve(input.repoRoot??defaultRepo);if(!input.evidenceDir)throw failure('FRESH_EVIDENCE_DIRECTORY_REQUIRED');const evidenceDir=path.resolve(input.evidenceDir);if(within(repoRoot,evidenceDir))throw failure('EVIDENCE_MUST_BE_OUTSIDE_REPOSITORY');
 const timeoutMs=input.timeoutMs??90000;if(!Number.isInteger(timeoutMs)||timeoutMs<500||timeoutMs>300000)throw failure('BOUNDED_TIMEOUT_REQUIRED');await fs.mkdir(evidenceDir,{recursive:false});
 const result={schemaVersion:1,id:'network.update-predownload-signals',runId:randomUUID(),status:'BLOCKED',startedAt:new Date().toISOString(),sourceChanges:['src/bridge/updatePredownloadService.ts','src/bridge/updatePredownloadScheduler.ts','src/bridge/updatePredownload.ts'],sourceHashes:[],actualCompiledInputs:[],checks:[],originalTests:[],executedProductModules:[],inputConsumerStateFeedback:[],remainingGaps:[],claims:{physicalHardwareValidated:false,operatingSystemSleepPerformed:false,installedApplicationExercised:false,actualDownloadPerformed:false,installedUpdate:false,userConfigurationRead:false,hostStarted:false}};
 try{
  const before=await snapshotUpdateDownloadInputs(repoRoot,getUpdateDownloadAdapters(repoRoot)[0].evidenceInputs);result.sourceHashes=before;if(before.some(item=>item.status!=='OK'))throw failure('REQUIRED_PRODUCT_OR_TOOL_INPUT_MISSING');
  const parentAuthority=lockBundledNodeTools(repoRoot);result.bundledToolAuthority=parentAuthority;
  result.process=await executeOwned({pwshPath:input.pwshPath??defaultPwsh,nodePath:process.execPath,repoRoot,evidenceDir,timeoutMs});if(result.process.timedOut)throw failure('UPDATE_DOWNLOAD_RUN_TIMEOUT');if(result.process.launchError)throw failure('OWNED_JOB_OR_PS7_UNAVAILABLE');if(result.process.outputTruncated)throw failure('OUTPUT_TRUNCATED');
  let worker;try{worker=await json(path.join(evidenceDir,'worker-result.json'));}catch{throw failure('UPDATE_DOWNLOAD_WORKER_RECEIPT_MISSING');}
  result.status='FAIL';result.toolVerification=verifyUpdateToolEvidence(worker,repoRoot,evidenceDir,parentAuthority);result.validation=validateUpdateDownloadReceipt(worker);result.checks=worker.checks??[];result.originalTests=worker.originalTests??[];result.stabilityTests=worker.stabilityTests??[];result.actualCompiledInputs=worker.compiledInputs??[];result.bundles=worker.bundles??[];result.executedProductModules=worker.executedProductModules??[];result.inputConsumerStateFeedback=worker.inputConsumerStateFeedback??[];result.injectedBoundary=worker.injectedBoundary;result.ioGuards=worker.ioGuards;result.compatibility=worker.compatibility;result.remainingGaps=worker.remainingGaps??[];result.failure=worker.failure??null;
  const combined=new Map(before.map(item=>[item.path,item]));for(const item of result.actualCompiledInputs){const prior=combined.get(item.path);if(prior&&prior.sha256!==item.sha256)throw failure('ACTUAL_COMPILED_INPUT_DIFFERS_FROM_SNAPSHOT');combined.set(item.path,{...item,status:'OK'});}result.sourceHashes=[...combined.values()].sort((a,b)=>a.path.localeCompare(b.path));result.sourceVerification=await verifyUpdateDownloadInputs(repoRoot,result.sourceHashes);
  if(result.process.exitCode===0&&result.validation.status==='PASS'&&result.sourceVerification.status==='PASS'&&result.toolVerification.status==='PASS')result.status='PASS';if(result.sourceVerification.status!=='PASS')result.failure={code:'SOURCE_CHANGED_DURING_UPDATE_DOWNLOAD_RUN',changed:result.sourceVerification.changed};
 }catch(error){result.status=error.code?.includes('CHANGED')||error.code?.includes('DIFFERS')?'FAIL':'BLOCKED';result.failure={code:error.code??error.message,exceptionType:error.name};result.remainingGaps.push('Required cached source/compiler/runtime/receipt missing; no successful fallback.');}
 if(result.status==='PASS'){result.ownedOutputs=ownedUpdateOutputs(evidenceDir);result.sourceHashes.push(...result.ownedOutputs);}
 result.finishedAt=new Date().toISOString();await fs.writeFile(path.join(evidenceDir,'result.json'),JSON.stringify(result,null,2));return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const mapping={'--repo-root':'repoRoot','--evidence-dir':'evidenceDir','--timeout-ms':'timeoutMs','--pwsh-path':'pwshPath'},args={};for(let i=2;i<process.argv.length;i+=2){const key=mapping[process.argv[i]];if(!key||process.argv[i+1]===undefined)throw failure('UNKNOWN_OR_MISSING_ARGUMENT');args[key]=key==='timeoutMs'?Number(process.argv[i+1]):process.argv[i+1];}const result=await runUpdateDownloadRegressions(args);console.log(JSON.stringify({status:result.status,checks:result.checks.length,originalChecks:result.originalTests.reduce((n,item)=>n+item.checks,0),failure:result.failure??null}));process.exitCode=result.status==='PASS'?0:result.status==='FAIL'?1:2;
 }catch(error){console.error(JSON.stringify({status:'BLOCKED',code:error.code??error.name}));process.exitCode=2;}
}

