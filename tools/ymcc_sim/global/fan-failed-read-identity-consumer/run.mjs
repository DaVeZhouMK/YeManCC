import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';
import {MODULE,CASE_ID,REPO,failedReadTools,getFanFailedReadIdentityAdapters} from './adapter.mjs';import {need,within,verifyReport,verifyColdSourceFaultReport,verifyOriginalIdentityAssertion} from './contract.mjs';
const read=file=>JSON.parse(fs.readFileSync(file,'utf8')),write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n'),sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
export function parseArgs(args){need(args.length===2&&args[0]==='--evidence-dir'&&typeof args[1]==='string'&&path.isAbsolute(args[1])&&!/[\0\r\n]/.test(args[1]),'FAN_FAILED_READ_ARGUMENTS');const root=path.resolve(args[1]);need(!within(REPO,root)&&!fs.existsSync(root),'FAN_FAILED_READ_FRESH_EXTERNAL_EVIDENCE');return {root};}
function walk(root){const files=[];function visit(dir){for(const row of fs.readdirSync(dir,{withFileTypes:true})){if(row.name==='node_modules')continue;need(!row.isSymbolicLink(),'FAN_FAILED_READ_OUTPUT_REPARSE');const file=path.join(dir,row.name);if(row.isDirectory())visit(file);else if(row.isFile())files.push(file);else throw Error('FAN_FAILED_READ_OUTPUT_TYPE');need(files.length<=650,'FAN_FAILED_READ_OUTPUT_LIMIT');}}visit(root);return files.sort();}
const arms=[['positive-original','positive',reportSpec=>verifyReport(...reportSpec)],['negative-retired-read','negative',([report,spec])=>verifyReport(report,spec,{fault:true})],['negative-cold-source-reentry','coldSourceFault',reportSpec=>verifyColdSourceFaultReport(...reportSpec)]];
export function collectClosingEvidence(repo,root,sources,tools,spec){
 const pending=new Map();
 for(const row of sources){
  need(sha(path.join(repo,row.path))===row.sha256,'FAN_FAILED_READ_SOURCE_CHANGED_DURING_RUN');
  pending.set(path.join(root,'input-sources',row.path),row.sha256);
 }
 for(const row of tools)need(sha(row.path)===row.sha256,'FAN_FAILED_READ_TOOL_CHANGED_DURING_RUN');
 const workerFile=path.join(root,'worker-result.json');
 const reports=new Map();
 for(const [label] of arms)reports.set(path.join(root,label,'original-report.json'),null);
 let worker;
 const inventory=walk(root).map(file=>{
  let sha256;
  if(file===workerFile||reports.has(file)){
   const bytes=fs.readFileSync(file),parsed=JSON.parse(bytes.toString('utf8'));
   if(file===workerFile)worker=parsed;else reports.set(file,parsed);
   sha256=crypto.createHash('sha256').update(bytes).digest('hex');
  }else sha256=sha(file);
  if(pending.has(file)){
   need(sha256===pending.get(file),'FAN_FAILED_READ_SOURCE_CHANGED_DURING_RUN');
   pending.delete(file);
  }
  return {path:file,sha256};
 });
 need(pending.size===0,'FAN_FAILED_READ_SNAPSHOT_OUTPUT_MISSING');
 need(worker?.status==='PASS'&&worker.passed===true&&worker.original45CasesRetained===true&&worker.coldSourceFault?.failedRows===1&&worker.productChanges===0&&worker.physicalAcceptance===false,'FAN_FAILED_READ_WORKER_INCOMPLETE');
 for(const [label,key,verify] of arms){
  const run=worker[key],file=path.join(root,label,'original-report.json');
  need(run?.originalReport===file,'FAN_FAILED_READ_OWNED_REPORT_PATH');
  need(run.compilerInputs===spec.buildInputs.length&&run.capturedActualInputs===spec.buildInputs.length&&run.metadataObserverBytesUnchanged===true&&run.rebuildExecuted===false,'FAN_FAILED_READ_WORKER_COMPILER_RECEIPT');
  const report=reports.get(file);
  need(report,'FAN_FAILED_READ_ORIGINAL_REPORT_MISSING');
  const checked=verify([report,spec]);
  for(const key of Object.keys(checked))need(JSON.stringify(checked[key])===JSON.stringify(run[key]),'FAN_FAILED_READ_WORKER_REPORT_MISMATCH');
 }
 return {worker,sourceHashes:[...sources,...inventory]};
}
export async function runFailedRead(root){
 need(path.isAbsolute(root)&&!within(REPO,root)&&!fs.existsSync(root),'FAN_FAILED_READ_FRESH_EXTERNAL_EVIDENCE');const spec=read(path.join(REPO,MODULE,'spec.json')),boundary=read(path.join(REPO,MODULE,'boundary.json')),tool=failedReadTools(REPO,spec),[adapter]=getFanFailedReadIdentityAdapters();need(fs.realpathSync(process.execPath)===fs.realpathSync(tool.nodeExecutable),'FAN_FAILED_READ_NODE_IDENTITY');for(const row of tool.files)need(sha(row.path)===row.sha256,'FAN_FAILED_READ_TOOL_CHANGED');const reviewed=new Map(boundary.executionSourcePins.map(row=>[row.path,row.sha256]));need(reviewed.size===boundary.executionSourcePins.length&&reviewed.size===spec.sources.length&&spec.sources.every(file=>reviewed.has(file)),'FAN_FAILED_READ_REVIEWED_SOURCE_SET');verifyOriginalIdentityAssertion(fs.readFileSync(path.join(REPO,spec.original),'utf8'),spec);const sources=adapter.evidenceInputs.map(relative=>{need(!path.isAbsolute(relative)&&!relative.split(/[\\/]/).includes('..'),'FAN_FAILED_READ_SOURCE_PATH');need(within(fs.realpathSync(REPO),fs.realpathSync(path.join(REPO,relative))),'FAN_FAILED_READ_SOURCE_ESCAPE');const sha256=sha(path.join(REPO,relative));if(reviewed.has(relative))need(sha256===reviewed.get(relative),'FAN_FAILED_READ_REVIEWED_SOURCE_CHANGED:'+relative);return {path:relative,sha256};});fs.mkdirSync(root,{recursive:true});
 try{
  for(const row of sources){const to=path.join(root,'input-sources',row.path);fs.mkdirSync(path.dirname(to),{recursive:true});fs.copyFileSync(path.join(REPO,row.path),to);need(sha(to)===row.sha256,'FAN_FAILED_READ_SNAPSHOT_DRIFT');}const frozen=path.join(root,'input-sources',MODULE),argsFile=path.join(root,'job-arguments.json');const jobReceipt=path.join(root,'job-receipt.json'),deadline=Date.now()+75000;write(argsFile,[path.join(frozen,'worker.mjs'),root]);const args=['-NoLogo','-NoProfile','-NonInteractive','-File',path.join(root,'input-sources/tools/ymcc_sim/global/host-process.ps1'),'-ProgramPath',tool.nodeExecutable,'-ArgumentsPath',argsFile,'-DeadlineEpochMs',String(deadline),'-CleanupReserveMs','1500','-DeadlineReceiptPath',jobReceipt],env={...process.env};delete env.NODE_OPTIONS;delete env.NODE_PATH;delete env.NODE_V8_COVERAGE;delete env.YMCC_FAN_FAILED_READ_CASE;const job=spawnSync(tool.powershellExecutable,args,{cwd:REPO,env,windowsHide:true,encoding:'utf8',timeout:75000,maxBuffer:4*1024*1024});fs.writeFileSync(path.join(root,'job.stdout.log'),job.stdout??'');fs.writeFileSync(path.join(root,'job.stderr.log'),job.stderr??'');write(path.join(root,'owned-job.json'),{program:tool.powershellExecutable,args,exitCode:job.status,error:job.error?.code??null,signal:job.signal,ownedJobObserved:(job.stdout??'').includes('YMCC_SIM_OWNED_JOB childPid=')});need(job.status===0&&!job.error&&!job.signal&&(job.stdout??'').includes('YMCC_SIM_OWNED_JOB childPid='),'FAN_FAILED_READ_OWNED_JOB_INCOMPLETE');const ended=read(jobReceipt);need(ended.status==='EXITED'&&ended.rootExitCode===0&&ended.jobAssigned===true&&ended.childExitedBeforeReturn===true&&ended.zeroObservedBeforeDispose===true&&ended.activeProcessesFinal===0&&ended.terminationRequested===false,'FAN_FAILED_READ_OWNED_JOB_NOT_NATURAL_ZERO');
  const {worker,sourceHashes}=collectClosingEvidence(REPO,root,sources,tool.files,spec);
  const result={schemaVersion:1,id:CASE_ID,status:'PASS',passed:true,originalChecks:45,originalAssertCallSites:spec.originalAssertCallSites,positive:worker.positive,negative:worker.negative,coldSourceFault:worker.coldSourceFault,sourceHashes,sourceChanges:[],compilerInputs:spec.buildInputs.length,executionScope:{original45CasesRetained:true,exactAll45OriginalCases:true,coldSourceRetirementBoundaries:8,publicColdOpenReentry:true,targetedColdSourceNegative:1,realFanHostLifecycle:true,originalNoIoHostAndInertLauncher:true,noParallelBehaviorModel:true,privateSourceFaultPredictedBeforeRun:true,compilerGraphIsNotAllBranchesOrProvidersExecution:true},inputConsumerStateFeedback:{input:'Original failed apply/preset/heartbeat, held write/read Promises, newer curves/OFF/Close/sleep/generation/resuming boundaries and queued retries',consumer:'Actual FanHostLifecycle public mutation/heartbeat queues and resync pre-read identity gate',state:'Retired failure must not query/adopt old remote state, old55 curve must not retry, fresh75 intent remains reachable and truly current closed sessions reopen once',feedback:'Original complete45-result JSON and predicted12 exact original assertion49 detections after private gate removal'},boundary:{ownedWindowsJob:true,originalRuntimeGuardReused:true,nativeWebViewAbsent:true,actualNativePostMessageCoverage:0,realHttp:false,realHost:false,realProcessLookupOrControl:false,realSettings:false,deviceIo:false,osSleep:false,productChanges:0,physicalAcceptance:false},remainingGaps:['Original NoIoHost family label does not prove a physical OEM device','Actual native/HC close effects and hardware readback remain separate backend/machine acceptance','Node V8 coverage proves observed actual call sites, not whole module/provider coverage'],fullProductAcceptance:false,fullGoalComplete:false};write(path.join(root,'result.json'),result);return result;
 }catch(error){write(path.join(root,'failure.json'),{status:'FAIL',message:error.message,firstOriginalFailurePreserved:true,noAutomaticRetry:true});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){try{const {root}=parseArgs(process.argv.slice(2)),result=await runFailedRead(root);console.log(JSON.stringify({id:CASE_ID,status:result.status,originalChecks:45,negativeFailures:result.negative.failedRows,sourceHashes:result.sourceHashes.length}));}catch(error){console.error(error.stack);process.exitCode=2;}}
