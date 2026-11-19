import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';
import {MODULE,CASE_ID,REPO,adoptedTools,getFanAdoptedStartRefusalAdapters} from './adapter.mjs';import {need,within,verifyReport,verifyOriginalIdentityAssertion} from './contract.mjs';
const read=file=>JSON.parse(fs.readFileSync(file,'utf8')),write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n'),sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
export function parseArgs(args){need(args.length===2&&args[0]==='--evidence-dir'&&typeof args[1]==='string'&&path.isAbsolute(args[1])&&!/[\0\r\n]/.test(args[1]),'FAN_ADOPTED_ARGUMENTS');const root=path.resolve(args[1]);need(!within(REPO,root)&&!fs.existsSync(root),'FAN_ADOPTED_FRESH_EXTERNAL_EVIDENCE');return {root};}
function walk(root){const files=[];function visit(dir){for(const row of fs.readdirSync(dir,{withFileTypes:true})){if(row.name==='node_modules')continue;need(!row.isSymbolicLink(),'FAN_ADOPTED_OUTPUT_REPARSE');const file=path.join(dir,row.name);if(row.isDirectory())visit(file);else if(row.isFile())files.push(file);else throw Error('FAN_ADOPTED_OUTPUT_TYPE');need(files.length<=650,'FAN_ADOPTED_OUTPUT_LIMIT');}}visit(root);return files.sort();}
export function collectClosingEvidence(repo,root,sources,tools,worker,spec){
 need(worker?.status==='PASS'&&worker.passed===true&&worker.original630LinesUnchanged===true&&worker.productChanges===0&&worker.physicalAcceptance===false,'FAN_ADOPTED_WORKER_INCOMPLETE');
 const reports=new Map();
 for(const [label,run,fault] of [['positive-original',worker.positive,false],['negative-cancelled-launch',worker.negative,true]]){
  const file=path.join(root,label,'original-report.json');
  need(run?.originalReport===file,'FAN_ADOPTED_OWNED_REPORT_PATH');
  need(run.compilerInputs===spec.buildInputs.length&&run.capturedActualInputs===spec.buildInputs.length&&run.metadataObserverBytesUnchanged===true&&run.rebuildExecuted===false,'FAN_ADOPTED_WORKER_COMPILER_RECEIPT');
  reports.set(file,{run,fault});
 }
 const pending=new Map();
 for(const row of sources){
  need(sha(path.join(repo,row.path))===row.sha256,'FAN_ADOPTED_SOURCE_CHANGED_DURING_RUN');
  pending.set(path.join(root,'input-sources',row.path),row.sha256);
 }
 for(const row of tools)need(sha(row.path)===row.sha256,'FAN_ADOPTED_TOOL_CHANGED_DURING_RUN');
 const inventory=walk(root).map(file=>{
  const report=reports.get(file);
  let sha256;
  if(report){
   const bytes=fs.readFileSync(file), checked=verifyReport(JSON.parse(bytes.toString('utf8')),spec,{fault:report.fault});
   for(const key of Object.keys(checked))need(JSON.stringify(checked[key])===JSON.stringify(report.run[key]),'FAN_ADOPTED_WORKER_REPORT_MISMATCH');
   sha256=crypto.createHash('sha256').update(bytes).digest('hex');
   reports.delete(file);
  }else sha256=sha(file);
  if(pending.has(file)){
   need(sha256===pending.get(file),'FAN_ADOPTED_SOURCE_CHANGED_DURING_RUN');
   pending.delete(file);
  }
  return {path:file,sha256};
 });
 need(pending.size===0,'FAN_ADOPTED_SNAPSHOT_OUTPUT_MISSING');
 need(reports.size===0,'FAN_ADOPTED_ORIGINAL_REPORT_MISSING');
 return [...sources,...inventory];
}
export async function runAdopted(root){
 need(path.isAbsolute(root)&&!within(REPO,root)&&!fs.existsSync(root),'FAN_ADOPTED_FRESH_EXTERNAL_EVIDENCE');const spec=read(path.join(REPO,MODULE,'spec.json')),boundary=read(path.join(REPO,MODULE,'boundary.json')),tool=adoptedTools(REPO,spec),[adapter]=getFanAdoptedStartRefusalAdapters();need(fs.realpathSync(process.execPath)===fs.realpathSync(tool.nodeExecutable),'FAN_ADOPTED_NODE_IDENTITY');for(const row of tool.files)need(sha(row.path)===row.sha256,'FAN_ADOPTED_TOOL_CHANGED');const reviewed=new Map(boundary.executionSourcePins.map(row=>[row.path,row.sha256]));need(reviewed.size===boundary.executionSourcePins.length&&reviewed.size===spec.sources.length&&spec.sources.every(file=>reviewed.has(file)),'FAN_ADOPTED_REVIEWED_SOURCE_SET');verifyOriginalIdentityAssertion(fs.readFileSync(path.join(REPO,spec.original),'utf8'),spec);const sources=adapter.evidenceInputs.map(relative=>{need(!path.isAbsolute(relative)&&!relative.split(/[\\/]/).includes('..'),'FAN_ADOPTED_SOURCE_PATH');need(within(fs.realpathSync(REPO),fs.realpathSync(path.join(REPO,relative))),'FAN_ADOPTED_SOURCE_ESCAPE');const sha256=sha(path.join(REPO,relative));if(reviewed.has(relative))need(sha256===reviewed.get(relative),'FAN_ADOPTED_REVIEWED_SOURCE_CHANGED:'+relative);return {path:relative,sha256};});fs.mkdirSync(root,{recursive:true});
 try{
  for(const row of sources){const to=path.join(root,'input-sources',row.path);fs.mkdirSync(path.dirname(to),{recursive:true});fs.copyFileSync(path.join(REPO,row.path),to);need(sha(to)===row.sha256,'FAN_ADOPTED_SNAPSHOT_DRIFT');}const frozen=path.join(root,'input-sources',MODULE),argsFile=path.join(root,'job-arguments.json'),jobReceipt=path.join(root,'job-receipt.json');write(argsFile,[path.join(frozen,'worker.mjs'),root]);const args=['-NoLogo','-NoProfile','-NonInteractive','-File',path.join(root,'input-sources/tools/ymcc_sim/global/host-process.ps1'),'-ProgramPath',tool.nodeExecutable,'-ArgumentsPath',argsFile,'-DeadlineReceiptPath',jobReceipt],env={...process.env};delete env.NODE_OPTIONS;delete env.NODE_PATH;delete env.NODE_V8_COVERAGE;const job=spawnSync(tool.powershellExecutable,args,{cwd:REPO,env,windowsHide:true,encoding:'utf8',timeout:140000,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(root,'job.stdout.log'),job.stdout??'');fs.writeFileSync(path.join(root,'job.stderr.log'),job.stderr??'');write(path.join(root,'owned-job.json'),{program:tool.powershellExecutable,args,exitCode:job.status,error:job.error?.code??null,signal:job.signal,ownedJobObserved:(job.stdout??'').includes('YMCC_SIM_OWNED_JOB childPid=')});need(job.status===0&&!job.error&&!job.signal&&(job.stdout??'').includes('YMCC_SIM_OWNED_JOB childPid='),'FAN_ADOPTED_OWNED_JOB_INCOMPLETE');const ended=read(jobReceipt);need(ended.status==='EXITED'&&ended.rootExitCode===0&&ended.jobAssigned===true&&ended.childExitedBeforeReturn===true&&ended.zeroObservedBeforeDispose===true&&ended.activeProcessesFinal===0&&ended.terminationRequested===false,'FAN_ADOPTED_OWNED_JOB_NOT_NATURAL_ZERO');
  const worker=read(path.join(root,'worker-result.json')),sourceHashes=collectClosingEvidence(REPO,root,sources,tool.files,worker,spec);
  const result={schemaVersion:1,id:CASE_ID,status:'PASS',passed:true,originalChecks:327,originalAssertCallSites:spec.originalAssertCallSites,originalGroups:31,positive:worker.positive,negative:worker.negative,sourceHashes,sourceChanges:[],compilerInputs:spec.buildInputs.length,executionScope:{original630LinesUnchanged:true,all327OrderedCases:true,realFanHostLifecycle:true,fiveFamiliesAreSharedFacadeFixtures:true,originalMemoryProcessProbeCapturedBeforeDynamicImport:true,noParallelBehaviorModel:true,sourceFaultOracleFrozenBeforeRun:true,compilerGraphIsNotEveryBranchOrProviderExecution:true},inputConsumerStateFeedback:{input:'Original cold/adopted refusals, held reads/handshakes/native replies and exact memory owner identity under wake/session/process/OFF/Close changes',consumer:'Actual FanHostLifecycle public startup/adoption/manual control and post-launch startup identity fence',state:'Declared refusal stays denied, ordinary uncertainty stays distinct, current manual remains reachable, obsolete completion cannot publish or stop another owner',feedback:'Original ordered327 JSON with hardwareCalls0 and exactly two original625 assertions after private first post-launch check removal'},boundary:{ownedWindowsJob:true,originalRuntimeGuardReused:true,frozenRuntimeObserverReused:true,nativeWebViewAbsent:true,actualNativePostMessageCoverage:0,actualRealProcessFindExactCalls:0,memoryProcessProbeCallsSeparate:true,realHttp:false,realHost:false,realProcessControl:false,realSettings:false,deviceIo:false,osSleep:false,productChanges:0,physicalAcceptance:false},remainingGaps:['Five fixture labels do not prove physical OEM hardware behavior','Memory exact-process identity is not a real Host instance/process control or native recovery acceptance','Source graph and observed native-zero counters are not whole-module/provider coverage'],fullProductAcceptance:false,fullGoalComplete:false};write(path.join(root,'result.json'),result);return result;
 }catch(error){write(path.join(root,'failure.json'),{status:'FAIL',message:error.message,firstOriginalFailurePreserved:true,noAutomaticRetry:true});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){try{const {root}=parseArgs(process.argv.slice(2)),result=await runAdopted(root);console.log(JSON.stringify({id:CASE_ID,status:result.status,originalChecks:327,negativeFailures:result.negative.failedRows,memoryProbeCalls:result.positive.capturedMemoryProcessProbeCalls,sourceHashes:result.sourceHashes.length}));}catch(error){console.error(error.stack);process.exitCode=2;}}
