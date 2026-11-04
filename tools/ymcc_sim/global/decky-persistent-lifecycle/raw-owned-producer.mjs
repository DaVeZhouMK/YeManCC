// One implementation of the existing owned RawProgram protocol; importing performs no work.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const key=file=>path.resolve(file).toLowerCase();
const pin=file=>({path:path.resolve(file),sha256:hash(file)});
export function verifyRawProducerCompletion(terminal,toolCleanupPolicy,toolArtifacts){
 assert.equal(terminal.collectionComplete,true,'RAW_COMPLETION_NOT_COLLECTED');
 assert.equal(terminal.job.activeProcessesFinal,0,'RAW_COMPLETION_ACTIVE_PROCESSES');assert.equal(terminal.job.zeroObservedBeforeDispose,true);assert.equal(terminal.job.disposed,true);
 if(terminal.reason==='NATURAL'){assert.equal(terminal.job.terminateRequested,false,'RAW_NATURAL_FORCE_TERMINATION');return {kind:'NATURAL',forcedToolFinalization:false,productResourcesWaived:false};}
 assert.equal(terminal.reason,'DECLARED_TOOL_CLEANUP','RAW_COMPLETION_REASON');
 assert(toolCleanupPolicy&&toolCleanupPolicy.kind==='MSVC_VCTIP_AFTER_ROOT_EXIT'&&toolCleanupPolicy.maximumProcesses===1,'RAW_TOOL_POLICY_REQUIRED');
 assert.deepEqual(terminal.declaredToolCleanup.policy,toolCleanupPolicy,'RAW_TOOL_POLICY_MISMATCH');assert.equal(terminal.declaredToolCleanup.eligible,true);assert.equal(terminal.declaredToolCleanup.productResourcesWaived,false);assert.equal(terminal.declaredToolCleanup.treatedAsNaturalExit,false);
 assert.equal(terminal.job.terminateRequested,true);assert.equal(terminal.job.terminateSucceeded,true,'RAW_TOOL_TERMINATION_FAILED');assert.equal(terminal.job.memberObservationError,null);
 const members=terminal.declaredToolCleanup.verifiedMembers;assert.equal(members.length,1,'RAW_TOOL_MEMBER_COUNT');assert.equal(terminal.job.membersBeforeTermination.length,1,'RAW_TOOL_ACTUAL_MEMBER_COUNT');
 const member=members[0],observed=terminal.job.membersBeforeTermination[0],expected=toolCleanupPolicy.program;
 assert.equal(path.basename(expected.path).toLowerCase(),'vctip.exe');assert.equal(key(member.program.path),key(expected.path));assert.equal(member.program.sha256,expected.sha256);assert.equal(member.ownedAtQuery,true);
 assert.equal(member.pid,observed.pid);assert.equal(member.startFileTimeUtc,observed.startFileTimeUtc);assert.equal(observed.observed,true);assert.equal(observed.ownedAtQuery,true);assert.equal(key(observed.image),key(expected.path));
 const pinned=toolArtifacts.find(row=>key(row.path)===key(expected.path));assert(pinned&&pinned.sha256===expected.sha256,'RAW_TOOL_UNDECLARED');
 return {kind:'DECLARED_TOOL_FINALIZATION',forcedToolFinalization:true,productResourcesWaived:false,member};
}
export function startOwnedRawProducer({id,program,args,cwd,env,host,powershell,ownerStartTicks,directory,stdoutPath,stderrPath,deadlineEpochMs,cleanupReserveMs=5000,sourceInputs,toolArtifacts,originalBudgetMs,parentRemainingBeforeMs,toolCleanupPolicy=null}){
 assert(path.isAbsolute(directory)&&!fs.existsSync(directory),'RAW_PRODUCER_FRESH_DIRECTORY');
 assert(/^\d{17,20}$/.test(String(ownerStartTicks)),'RAW_PRODUCER_OWNER_BIRTH');
 fs.mkdirSync(directory,{recursive:true});assert.equal(key(fs.realpathSync(directory)),key(directory),'RAW_PRODUCER_REPARSE');
 const argumentsFile=path.join(directory,'arguments.json'),requestFile=path.join(directory,'request.json'),nonce=crypto.randomBytes(32).toString('hex');
 fs.writeFileSync(argumentsFile,JSON.stringify(args)+'\n',{flag:'wx'});
 const environment={};for(const [name,value]of Object.entries(env)){assert.equal(typeof value,'string');environment[name]=value;}
 const request={schemaVersion:1,kind:'ymcc-raw-program-v1',cancelNonce:nonce,cwd,arguments:{...pin(argumentsFile)},environment,sources:sourceInputs,tools:toolArtifacts,...(toolCleanupPolicy?{toolCleanupPolicy}:{}),maxOutputBytes:16*1024*1024};
 fs.writeFileSync(requestFile,JSON.stringify(request)+'\n',{flag:'wx'});
 const launchArgs=['-NoLogo','-NoProfile','-NonInteractive','-File',host,'-RawProgram','-ProgramPath',program,'-ArgumentsPath',argumentsFile,'-RawDirectory',directory,'-RawRequestPath',requestFile,'-RawRequestSha256',hash(requestFile),'-OwnerProcessId',String(process.pid),'-OwnerStartTicks',String(ownerStartTicks),'-DeadlineEpochMs',String(deadlineEpochMs),'-CleanupReserveMs',String(cleanupReserveMs)];
 const requested=Date.now(),child=spawn(powershell,launchArgs,{cwd,env,windowsHide:true,stdio:['ignore','pipe','pipe']});
 const helperOut=[],helperErr=[];let helperBytes=0,helperError=null,done=false,cancelWritten=false;
 const collect=(rows,data)=>{helperBytes+=data.length;if(helperBytes<=1024*1024)rows.push(Buffer.from(data));};
 child.stdout.on('data',data=>collect(helperOut,data));child.stderr.on('data',data=>collect(helperErr,data));
 child.on('error',error=>{helperError=String(error.code??error.message);});
 let timer;
 const wait=new Promise(resolve=>child.once('close',(helperExit,helperSignal)=>{
  done=true;clearTimeout(timer);const finish=Date.now(),terminalFile=path.join(directory,'terminal.json');let terminal=null,error=helperError,proof;
  try{
   fs.writeFileSync(path.join(directory,'helper.stdout.raw'),Buffer.concat(helperOut),{flag:'wx'});fs.writeFileSync(path.join(directory,'helper.stderr.raw'),Buffer.concat(helperErr),{flag:'wx'});
   assert(helperBytes<=1024*1024,'RAW_HELPER_OUTPUT_LIMIT');assert.equal(helperSignal,null,'RAW_HELPER_SIGNAL');
   terminal=read(terminalFile);assert.equal(terminal.kind,'ymcc-raw-program-terminal-v1');assert.equal(terminal.collectionComplete,true,'RAW_COLLECTION_INCOMPLETE');
   assert.equal(terminal.declaredPinsUnchanged,true,'RAW_DECLARED_INPUT_DRIFT');assert.equal(terminal.logsClosedAndFlushed,true,'RAW_LOGS_NOT_CLOSED');
   assert.equal(terminal.job.zeroObservedBeforeDispose,true,'RAW_JOB_ZERO_NOT_OBSERVED');assert.equal(terminal.job.activeProcessesFinal,0,'RAW_DESCENDANTS_LIVE');assert.equal(terminal.job.disposed,true,'RAW_JOB_NOT_DISPOSED');
   assert.equal(terminal.job.cleanupError,null,'RAW_JOB_CLEANUP_ERROR');assert.equal(terminal.business.atomicJobAdmission,true,'RAW_JOB_ADMISSION');assert.equal(terminal.launcher.pid,child.pid,'RAW_LAUNCHER_PID');assert.notEqual(terminal.business.pid,child.pid,'RAW_BUSINESS_IS_NOT_LAUNCHER');assert.equal(key(terminal.launcher.program.path),key(powershell),'RAW_LAUNCHER_IMAGE');assert.equal(terminal.launcher.program.sha256,hash(powershell));assert.equal(terminal.launcher.helper.sha256,hash(host),'RAW_HELPER_SOURCE');
   assert.equal(key(terminal.business.program.path),key(program),'RAW_ACTUAL_PROGRAM');assert.equal(terminal.business.program.sha256,hash(program));assert.deepEqual(terminal.business.arguments,args);assert.equal(key(terminal.business.cwd),key(cwd));
   assert.equal(terminal.owner.pid,process.pid);assert.equal(terminal.owner.expectedStartTicks,String(ownerStartTicks));assert.equal(terminal.request.sha256,hash(requestFile));assert.equal(terminal.argumentsPin.sha256,hash(argumentsFile));
   const declaredBefore=terminal.declaredInputsBefore.map(row=>[row.role,key(row.path),row.sha256]);
   assert.deepEqual(declaredBefore,[...sourceInputs.map(row=>['sources',key(row.path),row.sha256]),...toolArtifacts.map(row=>['tools',key(row.path),row.sha256])],'RAW_INPUT_MEMBERSHIP');
   assert.equal(terminal.deadlineEpochMs,deadlineEpochMs);assert(terminal.finishedEpochMs<=deadlineEpochMs,'RAW_TERMINAL_OVER_DEADLINE');
   for(const [kind,target]of [['stdout',stdoutPath],['stderr',stderrPath]]){const raw=terminal.logs[kind];assert.equal(key(raw.path),key(path.join(directory,kind+'.raw')));assert.equal(hash(raw.path),raw.sha256);assert(!fs.existsSync(target),'RAW_CANONICAL_LOG_CONFLICT');fs.copyFileSync(raw.path,target,fs.constants.COPYFILE_EXCL);assert.equal(hash(target),raw.sha256);}
   const born=Number((BigInt(terminal.business.startFileTimeUtc)-116444736000000000n)/10000n);
   assert(born>=requested-100&&born<=finish,'RAW_BUSINESS_START_WINDOW');
   let completion=null;try{completion=verifyRawProducerCompletion(terminal,toolCleanupPolicy,toolArtifacts);}catch{}const natural=completion!==null;
   proof={id,pid:terminal.business.pid,startedAt:new Date(born).toISOString(),finishedAt:new Date(terminal.finishedEpochMs).toISOString(),program:terminal.business.program,arguments:args,cwd,originalBudgetMs,timeoutMs:Math.max(1,deadlineEpochMs-requested),parentRemainingBeforeMs,budgetClipped:deadlineEpochMs-requested<originalBudgetMs,elapsedMs:finish-requested,exitCode:terminal.businessExitCode,signal:null,error:natural?null:'RAW_PRODUCER_'+terminal.reason,timedOut:terminal.timedOut===true,outputTruncated:terminal.outputLimitExceeded===true,stdout:pin(stdoutPath),stderr:pin(stderrPath),rawExecution:{terminal:pin(terminalFile),request:pin(requestFile),arguments:pin(argumentsFile),helperSource:pin(host),launcher:terminal.launcher,helperPid:child.pid,helperExitCode:helperExit,helperSignal,actualBusiness:terminal.business,completion,job:terminal.job,logs:terminal.logs,sourceBefore:terminal.declaredInputsBefore,sourceAfter:terminal.declaredInputsAfter,collectionComplete:true,cancelled:terminal.cancelled===true,ownerLost:terminal.ownerLost===true,residualDescendants:terminal.residualDescendants===true}};
  }catch(caught){error=String(caught.stack??caught);proof={id,pid:terminal?.business?.pid??null,startedAt:new Date(requested).toISOString(),finishedAt:new Date(finish).toISOString(),program:pin(program),arguments:args,cwd,originalBudgetMs,timeoutMs:Math.max(1,deadlineEpochMs-requested),parentRemainingBeforeMs,elapsedMs:finish-requested,exitCode:null,signal:null,error,timedOut:terminal?.timedOut===true,outputTruncated:false,rawExecution:{terminal:fs.existsSync(terminalFile)?pin(terminalFile):null,helperPid:child.pid,helperExitCode:helperExit,helperSignal,collectionComplete:false}};}
  resolve(proof);
 }));
 function requestCancel(){if(done||cancelWritten)return;cancelWritten=true;const file=path.join(directory,'cancel.request.json');try{fs.writeFileSync(file,JSON.stringify({schemaVersion:1,kind:'CANCEL',nonce,ownerPid:process.pid,ownerStartTicks:String(ownerStartTicks)})+'\n',{flag:'wx'});}catch(error){if(error.code!=='EEXIST')throw error;}}
 // This is cancellation, never root kill. The raw helper owns the single subtree.
 timer=setTimeout(requestCancel,Math.max(1,deadlineEpochMs-cleanupReserveMs-Date.now()));
 return {wait:()=>wait,cancel:async()=>{requestCancel();const proof=await wait;if(proof.rawExecution.collectionComplete!==true)throw Error('RAW_CANCEL_COLLECTION_INCOMPLETE:'+id);return proof;},get pid(){return child.pid;}};
}
export function rawProgramLauncherDeclaration(environment=process.env){
 const configured=environment.YMCC_SIM_RAW_PROGRAM_PWSH;if(!configured)return null;
 assert(path.isAbsolute(configured),'RAW_LAUNCHER_ABSOLUTE_REQUIRED');const program=path.resolve(configured);
 assert.equal(path.basename(program).toLowerCase(),'pwsh.exe','RAW_LAUNCHER_CORE_REQUIRED');assert.equal(key(fs.realpathSync(program)),key(program),'RAW_LAUNCHER_REPARSE');
 const root=path.dirname(program),members=new Set([program]);
 for(const directory of [root,path.join(root,'ref')]){assert(fs.statSync(directory).isDirectory(),'RAW_LAUNCHER_RUNTIME_DIRECTORY_REQUIRED');for(const name of fs.readdirSync(directory)){const file=path.join(directory,name);if(fs.statSync(file).isFile()&&/\.(?:dll|json|exe)$/i.test(name)){assert.equal(key(fs.realpathSync(file)),key(file),'RAW_LAUNCHER_RUNTIME_REPARSE');members.add(file);}}}
 for(const module of ['Management','Utility','Security']){const file=path.join(root,'Modules','Microsoft.PowerShell.'+module,'Microsoft.PowerShell.'+module+'.psd1');assert(fs.statSync(file).isFile(),'RAW_LAUNCHER_MODULE_REQUIRED');assert.equal(key(fs.realpathSync(file)),key(file),'RAW_LAUNCHER_MODULE_REPARSE');members.add(file);}
 assert(members.size<=640,'RAW_LAUNCHER_MEMBER_LIMIT');return {program,files:[...members],scope:'Explicit installed PowerShellCore raw launcher runtime+AddType refs; not a product or second test producer'};
}
