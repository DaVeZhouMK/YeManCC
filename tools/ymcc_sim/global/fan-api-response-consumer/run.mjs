import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';import {MODULE,REPO,CASE_ID,moduleSources} from './adapter.mjs';
const read=file=>JSON.parse(fs.readFileSync(file,'utf8')),write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n'),sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'),need=(value,code)=>{if(!value)throw Error(code);},inside=(root,file)=>{const relative=path.relative(root,file);return relative===''||(relative!=='..'&&!relative.startsWith('..'+path.sep)&&!path.isAbsolute(relative));};
export function parseArgs(args){need(args.length===2&&args[0]==='--evidence-dir'&&typeof args[1]==='string'&&path.isAbsolute(args[1])&&!/[\0\r\n]/.test(args[1]),'FAN_RESPONSE_ARGUMENTS');const root=path.resolve(args[1]);need(!inside(REPO,root)&&!fs.existsSync(root),'FAN_RESPONSE_FRESH_EXTERNAL_EVIDENCE');return{root};}
export function verifyOriginalOutcome(raw,spec,{negative=false}={}){
 need(raw&&!raw.error&&!raw.signal&&raw.exitCode===(negative?1:0),'FAN_RESPONSE_ORIGINAL_PROCESS_NOT_COMPLETE');const boundary=raw.boundary,flags=['realNetwork','realChildProcess','nativeLibraryLoad','writesOutsideEvidence','realHostMain','hardwareAccess','operatingSystemSleep'];need(boundary?.kind==='fan-global-runtime-boundary'&&boundary.exitCode===raw.exitCode&&boundary.explicitExitCalled===false&&boundary.status===(negative?'FAIL':'PASS')&&Array.isArray(boundary.denied)&&boundary.denied.length===0&&flags.every(name=>boundary.boundaries?.[name]===false),'FAN_RESPONSE_RUNTIME_BOUNDARY');let report;try{report=JSON.parse(raw.stdout.trim());}catch{throw Error('FAN_RESPONSE_ORIGINAL_FINAL_JSON_REQUIRED');}
 need(report&&Object.keys(report).sort().join(',')==='checks,failed,failures,ok,scope'&&typeof report.ok==='boolean'&&Number.isSafeInteger(report.checks)&&report.checks>=0&&Number.isSafeInteger(report.failed)&&report.failed>=0&&Array.isArray(report.failures)&&report.failures.length===report.failed&&report.failures.every(value=>typeof value==='string'&&value.length<=4096)&&report.scope===spec.originalScope&&report.checks+report.failed===spec.expectedTotalChecks,'FAN_RESPONSE_ORIGINAL_REPORT_INCOMPLETE');
 if(negative)need(report.ok===false&&report.failed===1&&report.checks===spec.expectedTotalChecks-1&&report.failures[0]===spec.fault.expectedFailure,'FAN_RESPONSE_ORIGINAL_EXPECTED_FAULT_NOT_CAUGHT');else need(report.ok===true&&report.failed===0&&report.checks===spec.expectedTotalChecks,'FAN_RESPONSE_ORIGINAL_CHECK_FAILED');return report;
}
function walk(root){const files=[];function visit(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){if(entry.name==='node_modules')continue;const file=path.join(dir,entry.name);need(!entry.isSymbolicLink(),'FAN_RESPONSE_OUTPUT_REPARSE');if(entry.isDirectory())visit(file);else if(entry.isFile())files.push(file);else throw Error('FAN_RESPONSE_OUTPUT_TYPE');need(files.length<=400,'FAN_RESPONSE_OUTPUT_LIMIT');}}visit(root);return files.sort();}
export function collectClosingEvidence(repo,root,sources,tools,spec){
 const frozen=new Map();
 for(const row of sources){
  need(sha(path.join(repo,row.path))===row.sha256,'FAN_RESPONSE_SOURCE_CHANGED_DURING_RUN');
  frozen.set(path.join(root,'input-sources',row.path),row.sha256);
 }
 for(const tool of tools)need(sha(tool.path)===tool.sha256,'FAN_RESPONSE_TOOLCHAIN_CHANGED_DURING_RUN');
 const workerPath=path.join(root,'worker-result.json');
 const armFiles=['stdout.log','original-report.json','execution.json','runtime-boundary.json'];
 const labels=[['positive',false],['negative-heartbeat-identity',true]];
 const parsed=new Map([[workerPath,null],...labels.flatMap(([label])=>armFiles.map(file=>[path.join(root,label,file),null]))]);
 const inventory=walk(root).map(file=>{
  let sha256;
  if(parsed.has(file)){
   const bytes=fs.readFileSync(file),text=bytes.toString('utf8');
   parsed.set(file,path.basename(file)==='stdout.log'?text:JSON.parse(text));
   sha256=crypto.createHash('sha256').update(bytes).digest('hex');
  }else sha256=sha(file);
  if(frozen.has(file)){
   need(frozen.get(file)===sha256,'FAN_RESPONSE_SOURCE_CHANGED_DURING_RUN');
   frozen.delete(file);
  }
  return {path:file,sha256};
 });
 need(frozen.size===0,'FAN_RESPONSE_SNAPSHOT_OUTPUT_MISSING');
 need([...parsed.values()].every(value=>value!==null),'FAN_RESPONSE_OWNED_RAW_MISSING');
 const worker=parsed.get(workerPath);
 need(worker?.status==='PASS'&&worker.passed===true&&worker.productChanges===0&&worker.realNetwork===false,'FAN_RESPONSE_WORKER_INCOMPLETE');
 const digestByPath=new Map(inventory.map(row=>[row.path,row.sha256]));
 for(const [label,negative] of labels){
  const dir=path.join(root,label),value=name=>parsed.get(path.join(dir,name));
  const declared=worker[negative?'negative':'positive'],execution=value('execution.json');
  const program=path.join(dir,'program.mjs'),rebuild=path.join(dir,'metadata-disabled-rebuild.mjs');
  need(declared?.program===program&&declared.boundary===path.join(dir,'runtime-boundary.json')&&declared.metafile===path.join(dir,'metafile.json')&&execution.label===label&&execution.args?.at(-1)===program,'FAN_RESPONSE_OWNED_ARM_PATH');
  need(declared.actualInputCount===spec.buildInputs.length&&declared.observerPreservedProgramBytes===true&&declared.rebuildExecuted===false&&digestByPath.has(program)&&digestByPath.has(declared.metafile)&&digestByPath.get(program)===declared.programSha256&&digestByPath.get(rebuild)===declared.programSha256,'FAN_RESPONSE_COMPILER_RECEIPT_MISMATCH');
  const report=verifyOriginalOutcome({...execution,stdout:value('stdout.log'),boundary:value('runtime-boundary.json')},spec,{negative});
  const saved=value('original-report.json');
  need(Object.keys(saved).length===Object.keys(report).length&&Object.keys(report).every(key=>JSON.stringify(report[key])===JSON.stringify(declared[key])&&JSON.stringify(report[key])===JSON.stringify(saved[key])),'FAN_RESPONSE_WORKER_REPORT_MISMATCH');
 }
 return {worker,sourceHashes:[...sources,...inventory]};
}
export async function runFanResponse(root){need(path.isAbsolute(root)&&!inside(REPO,root)&&!fs.existsSync(root),'FAN_RESPONSE_FRESH_EXTERNAL_EVIDENCE');const spec=read(path.join(REPO,MODULE,'spec.json')),tool=read(path.join(REPO,MODULE,'toolchain-inputs.json'));need(fs.realpathSync(process.execPath)===fs.realpathSync(tool.nodeExecutable),'FAN_RESPONSE_NODE_IDENTITY');for(const file of tool.files)need(sha(file.path)===file.sha256,'FAN_RESPONSE_TOOLCHAIN_IDENTITY');const reviewed=new Map(spec.executionSourcePins.map(row=>[row.path,row.sha256]));need(reviewed.size===spec.executionSourcePins.length&&reviewed.size===spec.sources.length&&spec.sources.every(file=>reviewed.has(file)),'FAN_RESPONSE_REVIEWED_SOURCE_SET');const sources=[...new Set([...moduleSources(),...spec.sources])].map(relative=>{need(typeof relative==='string'&&!path.isAbsolute(relative)&&!relative.split(/[\\/]/).includes('..'),'FAN_RESPONSE_SOURCE_PATH');const actual=fs.realpathSync(path.join(REPO,relative));need(inside(fs.realpathSync(REPO),actual),'FAN_RESPONSE_SOURCE_ESCAPE');const sha256=sha(actual);if(reviewed.has(relative))need(reviewed.get(relative)===sha256,'FAN_RESPONSE_REVIEWED_SOURCE_CHANGED:'+relative);return{path:relative,sha256};});fs.mkdirSync(root,{recursive:true});
 try{for(const row of sources){const to=path.join(root,'input-sources',row.path);fs.mkdirSync(path.dirname(to),{recursive:true});fs.copyFileSync(path.join(REPO,row.path),to);need(sha(to)===row.sha256,'FAN_RESPONSE_SNAPSHOT_DRIFT');}const argsFile=path.join(root,'job-arguments.json'),frozen=path.join(root,'input-sources',MODULE);const deadline=Date.now()+60000,receipt=path.join(root,'job-receipt.json');write(argsFile,[path.join(frozen,'worker.mjs'),root]);const env={...process.env};delete env.NODE_OPTIONS;delete env.NODE_PATH;const job=spawnSync(tool.powershellExecutable,['-NoLogo','-NoProfile','-File',path.join(root,'input-sources/tools/ymcc_sim/global/host-process.ps1'),'-ProgramPath',tool.nodeExecutable,'-ArgumentsPath',argsFile,'-DeadlineEpochMs',String(deadline),'-CleanupReserveMs','1500','-DeadlineReceiptPath',receipt],{cwd:REPO,env,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:4*1024*1024});fs.writeFileSync(path.join(root,'job.stdout.log'),job.stdout??'');fs.writeFileSync(path.join(root,'job.stderr.log'),job.stderr??'');need(job.status===0&&!job.error&&!job.signal&&(job.stdout??'').includes('YMCC_SIM_OWNED_JOB childPid='),'FAN_RESPONSE_OWNED_JOB_NOT_COMPLETE');const ended=read(receipt);need(ended.status==='EXITED'&&ended.rootExitCode===0&&ended.jobAssigned===true&&ended.childExitedBeforeReturn===true&&ended.zeroObservedBeforeDispose===true&&ended.activeProcessesFinal===0&&ended.terminationRequested===false,'FAN_RESPONSE_JOB_NOT_NATURAL_ZERO');const {worker,sourceHashes}=collectClosingEvidence(REPO,root,sources,tool.files,spec);
 const result={schemaVersion:1,id:CASE_ID,status:'PASS',passed:true,originalChecks:81,positive:worker.positive,negative:worker.negative,sourceHashes,sourceChanges:[],executedProductModules:['src/bridge/fanApi.ts','src/bridge/api.ts','src/bridge/fanDiagnostics.ts','src/bridge/ipc.ts'],executionScope:{originalScriptAndAssertionsUnchanged:true,wholeVueOrModuleBranchCoverage:false,privateFaultIsCurrentCounterexampleNotHistoricalIncident:true},inputConsumerStateFeedback:{input:'Original fake HTTP bodies/statuses/timeouts, malformed receipts and lease identity rotation',consumer:'Actual HttpFanApiAdapter/API wrappers/diagnostic generation with original fake terminal',state:'Original strict handshake/state/lease/unknown-result/refusal/cancellation contracts',feedback:'Original81-check final report; private heartbeat admission fault yields exact original failure'},boundary:{ownedWindowsJob:true,realHttpNetwork:false,nativeBridgeProvided:false,hostMain:false,hardwareAccess:false,installedApplication:false,mainSourceRead:false,productChanges:0,physicalAcceptance:false,fullProductAcceptance:false},remainingGaps:['No actual WinHTTP/native IPC server/Host/device execution','81 named original checks are not complete route/module or physical coverage'],fullGoalComplete:false};write(path.join(root,'result.json'),result);return result;
 }catch(error){write(path.join(root,'failure.json'),{status:'FAIL',message:error.message,originalFailuresPreserved:true});throw error;}}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){try{const{root}=parseArgs(process.argv.slice(2)),result=await runFanResponse(root);console.log(JSON.stringify({id:CASE_ID,status:result.status,checks:result.originalChecks,sourceHashes:result.sourceHashes.length}));}catch(error){console.error(error.stack);process.exitCode=2;}}
