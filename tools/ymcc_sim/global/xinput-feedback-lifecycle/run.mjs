import {verifyExpandedCurrentCaseShape} from './current-case-shape.mjs';
import {mutateXinputSourceFault} from './fault-mutation.mjs';
import {runXinputFaults} from './fault-scheduler.mjs';
import {inspectCurrentStage,requireCurrentStageRecorded} from './stage-evidence.mjs';
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {fileURLToPath} from 'node:url';
import {REPO,MODULE,CASE_ID,PRODUCER,BASELINE_SHA256,moduleSources,productSources,toolFiles} from './adapter.mjs';
import {sourcePath,runProcess} from '../runner.mjs';
import {calibrationToolchainPreamble} from '../../../fixtures/hc_reviewed_calibration/toolchain.mjs';
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex'),hash=file=>sha(fs.readFileSync(file));
const need=(value,code)=>{if(!value)throw Error(code);},read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,'')),write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
function walk(root){return fs.readdirSync(root,{withFileTypes:true}).flatMap(entry=>{need(!entry.isSymbolicLink(),'XINPUT_OUTPUT_LINK');const file=path.join(root,entry.name);return entry.isDirectory()?walk(file):entry.isFile()?[file]:[];});}
export function parseArgs(args){need(Array.isArray(args)&&args.length>=4&&args[0]==='--mode'&&args[1]==='current'&&args[2]==='--evidence-dir'&&typeof args[3]==='string'&&args[3].length>0,'EXPLICIT_CURRENT_MODE_AND_OUTPUT_REQUIRED');const flags=args.slice(4);need(flags.every(flag=>['--private-snapshot','--discover-tools'].includes(flag))&&new Set(flags).size===flags.length,'UNKNOWN_OR_DUPLICATE_XINPUT_ARGUMENT');need(!flags.includes('--discover-tools')||flags.includes('--private-snapshot'),'TOOL_DISCOVERY_IS_NOT_FORMAL_EXECUTION');return{evidenceDir:args[3],privateSnapshot:flags.includes('--private-snapshot'),discoverTools:flags.includes('--discover-tools')};}
function stageRoot(root){const runFolder=path.join(root,'original/runs');need(fs.existsSync(runFolder),'ORIGINAL_XINPUT_RUN_DIRECTORY_MISSING');const dirs=fs.readdirSync(runFolder,{withFileTypes:true}).filter(row=>row.isDirectory());need(dirs.length===1,'ORIGINAL_XINPUT_RUN_NOT_UNIQUE');return path.join(runFolder,dirs[0].name);}
export function validateCurrentStage(root,execution,{negative=false,expectedCheck=null,sourceRoot}={}){
 need(Number.isInteger(execution.exitCode)&&!execution.signal&&!execution.timedOut&&!execution.launchError&&!execution.outputTruncated,'ORIGINAL_XINPUT_PROCESS_INCOMPLETE');
 const processProof=read(path.join(root,'original-process.json'));need(processProof.mode==='current'&&processProof.arguments[0]==='--current'&&processProof.arguments[2]==='--out','ORIGINAL_PROTOTYPE_MODE_EXECUTED');
 const run=stageRoot(root),ranges=read(path.join(run,'source-ranges.json'));need(path.resolve(ranges.currentFile)===path.resolve(sourceRoot,'native/main.cpp')&&path.resolve(processProof.arguments[1])===path.resolve(sourceRoot,'native/main.cpp'),'ORIGINAL_CURRENT_SOURCE_PATH_NOT_THE_RECORDED_SNAPSHOT');need(ranges.mode==='production'&&ranges.currentFileSha256===hash(path.join(sourceRoot,'native/main.cpp'))&&ranges.headerSha256===hash(path.join(sourceRoot,'native/xinput_identity_refresh_policy.h')),'ORIGINAL_CURRENT_SOURCE_IDENTITY_INVALID');
 for(const name of ['CURRENT','STOP','FINISH','READY','START','SCHEDULE','DISCONNECT','CLASSIFICATION','POLICY_HELPER'])need(/^[a-f0-9]{64}$/.test(ranges.parts?.[name]??''),'ACTUAL_CURRENT_CONSUMER_NOT_EXTRACTED:'+name);
 need(hash(path.join(root,'original/baseline-fragments.json'))===BASELINE_SHA256,'HISTORICAL_BASELINE_CHANGED');need(!fs.existsSync(path.join(root,'original/main-integration.patch')),'CANDIDATE_PATCH_MODE_NOT_ADMITTED');
 const stageEvidence=inspectCurrentStage(run,{negative});write(path.join(root,'original-stage-evidence.json'),stageEvidence);requireCurrentStageRecorded(run,{negative});
 const recorded=[];
 for(const label of ['old-off','old-on','fixed-off',...(negative?[]:['fixed-on'])]){
  const compile=read(path.join(run,label+'-build.command.json'));need(compile.status===0&&!compile.error&&!compile.signal&&!compile.timedOut,'COMPILER_FAILURE_IS_NOT_SEMANTIC_XINPUT_EVIDENCE');
  need(fs.existsSync(path.join(run,label+'-execute.command.json')),'ORIGINAL_XINPUT_EXECUTION_NOT_REACHED:'+label);
  const execute=read(path.join(run,label+'-execute.command.json'));need(!execute.error&&!execute.signal&&!execute.timedOut,'ORIGINAL_XINPUT_EXECUTION_PROCESS_ERROR:'+label);
  const report=read(path.join(run,label+'-execute.stdout.txt'));need(report.actualStartStopFinishAndScheduleExtracted===true&&report.deviceHostSdkExecuted===false&&report.caseCount>=35&&report.checks>=report.caseCount&&Array.isArray(report.cases)&&report.cases.length===report.caseCount,'ORIGINAL_XINPUT_BUSINESS_REPORT_MISSING');
  if(!negative&&!label.startsWith('old'))verifyExpandedCurrentCaseShape(report,sourceRoot);
  const failed=report.cases.filter(row=>row.passed===false);need(failed.length===report.failedCases,'ORIGINAL_XINPUT_FAILURE_SUMMARY_MISMATCH');
  if(label.startsWith('old'))need(execute.status===1&&report.failedCases>0,'HISTORICAL_FIXTURE_COUNTEREXAMPLE_NOT_DETECTED');
  else if(negative)need(execute.status===1&&report.failedCases>0&&(!expectedCheck||report.cases.some(row=>row.checks.some(check=>check.passed===false&&check.name===expectedCheck))),'CURRENT_SOURCE_FAULT_NOT_DETECTED_BY_ORIGINAL_ASSERTIONS');
  else need(execute.status===0&&report.failedCases===0&&report.failedChecks===0,'CURRENT_PRODUCTION_CONSUMERS_NOT_PASS');
  const imports=fs.readFileSync(path.join(run,label,'fixture.exe.imports.log'),'utf8');need(!/\b(?:xinput\w*|dinput\w*|hid|setupapi|winusb|vigem\w*|usbip\w*|HandheldCompanion|HIDMaestro)\.dll\b/i.test(imports)&&!/\bDeviceIoControl\b/i.test(imports),'UNADMITTED_DEVICE_IMPORT');
  const dlls=[...imports.matchAll(/^\s*([\w.-]+\.dll)\s*$/gmi)].map(row=>row[1].toLowerCase());need(dlls.length===1&&dlls[0]==='kernel32.dll','UNREVIEWED_RUNTIME_OR_DEVICE_IMPORT');
  recorded.push({label,compileExitCode:compile.status,exitCode:execute.status,caseCount:report.caseCount,checks:report.checks,failedCases:report.failedCases,failedChecks:report.failedChecks,failedCheckNames:[...new Set(failed.flatMap(row=>row.checks.filter(check=>!check.passed).map(check=>check.name)))]});
 }
 if(negative)need(execution.exitCode!==0,'MUTATED_CURRENT_SOURCE_UNEXPECTEDLY_PASSED');
 else{
  const summary=read(path.join(root,'original/summary.json'));need(execution.exitCode===0&&summary.passed===true&&summary.mode==='production'&&summary.source_range_drift===false&&summary.main_cpp_edited===false,'ORIGINAL_CURRENT_TERMINAL_SUMMARY_MISSING');
  need(execution.stdout.replaceAll('\r\n','\n').includes(JSON.stringify(summary,null,2)),'ORIGINAL_CURRENT_STDOUT_TERMINAL_NOT_OBSERVED');
  need(read(path.join(run,'scope-drift.json')).changed.length===0,'ACTUAL_CURRENT_FRAGMENT_CHANGED_DURING_EXECUTION');
  for(const row of recorded){const original=summary.profiles?.[row.label];need(original?.cases===row.caseCount&&original?.checks===row.checks&&original?.failedCases===row.failedCases,'ORIGINAL_CURRENT_PROFILE_SUMMARY_MISMATCH');}
 }
 return{status:negative?'EXPECTED_REJECTION':'PASS',mode:'production-current',run,profiles:recorded,originalBusinessAssertionsRetained:true,fixtureByteIdenticalToBeforeTotalBinding:false,realDeviceOrHostExecuted:false,historicalBaselineIsCompleteSourcePreimage:false};
}
export async function runLifecycle({evidenceDir,privateSnapshot=false,discoverTools=false}){
 const root=path.resolve(evidenceDir);need(!fs.existsSync(root),'FRESH_XINPUT_EVIDENCE_ROOT_REQUIRED');fs.mkdirSync(root,{recursive:true});
 const sourceFiles=[...new Set([...moduleSources(),...productSources(),'tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/ymcc_sim/global/controller-priority/adapter.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])];
 const sources=sourceFiles.map(relative=>{const original=sourcePath(REPO,relative),before=hash(original),bytes=fs.readFileSync(original);need(sha(bytes)===before&&hash(original)===before,'SOURCE_CHANGED_DURING_PRIVATE_CAPTURE');const copy=path.join(root,'input-sources',relative);fs.mkdirSync(path.dirname(copy),{recursive:true});fs.writeFileSync(copy,bytes);return{path:relative,sha256:before,copy};});
 need(hash(path.join(root,'input-sources',MODULE,'baseline-fragments.json'))===BASELINE_SHA256,'UNREVIEWED_HISTORICAL_BASELINE');
 const tools=toolFiles().map(file=>({path:file,sha256:hash(file)})),powershell=tools.find(row=>/pwsh\.exe$/i.test(row.path)).path,dumpbin=tools.find(row=>/dumpbin\.exe$/i.test(row.path)).path;
 const toolchain=path.join(root,'installed-toolchain.cmd');fs.writeFileSync(toolchain,calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat'));
 const steps=[],faultScheduling={};
 async function execute(label,sourceRoot,options={}){
  const dir=path.join(root,label);fs.mkdirSync(dir);fs.mkdirSync(path.join(dir,'original'));fs.copyFileSync(path.join(sourceRoot,MODULE,'baseline-fragments.json'),path.join(dir,'original/baseline-fragments.json'));
  const config=path.join(dir,'config.json'),args=path.join(dir,'arguments.json');write(config,{mode:'current',caseRoot:root,root:dir,sourceRoot,node:process.execPath,toolchainCmd:toolchain,dumpbin});write(args,[config]);
  const result=await runProcess(powershell,['-NoLogo','-NoProfile','-NonInteractive','-File',path.join(REPO,'tools/ymcc_sim/global/host-process.ps1'),'-ScriptPath',path.join(REPO,MODULE,'Run-Current.ps1'),'-ArgumentsPath',args],{cwd:dir,env:process.env,timeoutMs:100000});
  fs.writeFileSync(path.join(dir,'job-stdout.log'),result.stdout);fs.writeFileSync(path.join(dir,'job-stderr.log'),result.stderr);const {stdout,stderr,...raw}=result;steps.push({label,...raw,ownedWindowsJob:true});
  return validateCurrentStage(dir,result,{...options,sourceRoot});
 }
 try{
  const positive=await execute('positive',path.join(root,'input-sources'));
  const includes=new Set();for(const file of walk(root).filter(file=>/\.(?:stdout\.txt|stderr\.txt|log)$/i.test(file))){for(const line of fs.readFileSync(file,'utf8').matchAll(/^\s*[^:\r\n]{1,64}:\s*[^:\r\n]{1,64}:\s*([A-Za-z]:\\[^\r\n]+?)\s*\r?$/gm)){const full=path.resolve(line[1].trim());if(!full.toLowerCase().startsWith(root.toLowerCase()+path.sep))includes.add(full);}}
  need(includes.size>20,'ACTUAL_XINPUT_INCLUDE_OBSERVATION_MISSING');need([...includes].every(file=>/^C:\\Program Files \(x86\)\\(?:Microsoft Visual Studio\\2022\\BuildTools\\VC\\Tools\\MSVC|Windows Kits\\10)\\/i.test(file)&&fs.statSync(file).isFile()),'XINPUT_UNREVIEWED_COMPILER_INCLUDE_ROOT');
  write(path.join(root,'observed-tools.json'),{files:[...includes].sort(),hashes:[...includes].sort().map(path=>({path,sha256:hash(path)}))});
  if(discoverTools){const result={status:'PRIVATE_TOOL_DISCOVERY_NOT_ACCEPTANCE',positive,files:[...includes].sort(),sourceInputs:sources.map(({path,sha256})=>({path,sha256})),steps};write(path.join(root,'discovery.json'),result);return result;}
  need([...includes].every(file=>tools.some(row=>row.path.toLowerCase()===file.toLowerCase())),'XINPUT_COMPILER_INPUT_NOT_PINNED_BEFORE_EXECUTION');
  const faults=[{id:'unknown-stop-writes',before:'if(!known_)return Stop::Wait;',after:'if(!known_)return Stop::Ready;',expectedCheck:'no blind zero during Unknown'},{id:'different-physical-path-reused',before:'if(fold(a[i])!=fold(b[i]))return false;',after:'if(fold(a[i])!=fold(b[i]))return true;',expectedCheck:'old stop never reaches new occupant'}];
  faults.push(
   {id:'xinput-total-binding-receipt-lost',file:'native/main.cpp',before:'target.unifiedBinding=feedbackBinding;receipt.capabilityToken=feedbackBinding.token(); // XInput provenance',after:'target.unifiedBinding={};receipt.capabilityToken=0; // XInput provenance',expectedCheck:'actual XInput pulse keeps total capability'},
   {id:'msi-total-binding-receipt-lost',file:'native/main.cpp',before:'target.unifiedBinding=feedbackBinding;receipt.capabilityToken=feedbackBinding.token(); // MSI provenance',after:'target.unifiedBinding={};receipt.capabilityToken=0; // MSI provenance',expectedCheck:'actual MSI pulse keeps total capability'}
  );
  faults.push(
   {id:'msi-wrong-motor-order',file:'native/main.cpp',before:'resource.report[4]=(BYTE)smallMotor;',after:'resource.report[4]=(BYTE)largeMotor;',expectedCheck:'real MSI small motor preserves index four'},
   {id:'msi-command-interface-admitted',file:'native/main.cpp',before:'return usagePage == 0x01 && (usage == 0x04 || usage == 0x05);',after:'return true;',expectedCheck:'real MSI source rejection never attempts write'},
   {id:'msi-final-authority-unchecked',file:'native/main.cpp',before:'result.sourceStable=hardwareWriteAllowed()&&currentPowerGeneration()==power',after:'result.sourceStable=true&&currentPowerGeneration()==power',expectedCheck:'real MSI final authority is rechecked after open'}
  );
  faults.push(
   {id:'msi-zero-result-discarded',file:'native/main.cpp',before:'if(!stopped.ok&&stopped.managed&&',after:'if(false&&stopped.managed&&',expectedCheck:'failed MSI zero retains outstanding physical feedback owner'},
   {id:'msi-source-wake-omitted',file:'native/main.cpp',before:'return !pending||gamepadSerialScheduleFeedbackStop(pending,0);',after:'return !pending;',expectedCheck:'late MSI zero reclaims retained owner'}
  );
  const negatives=[];
  await runXinputFaults({faults,negatives,faultScheduling,execute:async(fault)=>{const mirror=path.join(root,'fault-source-'+fault.id);for(const row of sources){const target=path.join(mirror,row.path);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(row.copy,target);}const target=path.join(mirror,fault.file??'native/xinput_identity_refresh_policy.h'),original=fs.readFileSync(target,'utf8');const mutation=mutateXinputSourceFault(original,fault,{sourceRanges:read(path.join(positive.run,'source-ranges.json'))});write(path.join(root,'mutation-'+fault.id+'.json'),mutation.proof);fs.writeFileSync(target,mutation.source);const proof=await execute('negative-'+fault.id,mirror,{negative:true,expectedCheck:fault.expectedCheck});for(const relative of [PRODUCER,'tools/fixtures/xinput_identity_feedback_lifecycle_fixture.cpp'])need(hash(path.join(mirror,relative))===sources.find(row=>row.path===relative).sha256,'ORIGINAL_XINPUT_PRODUCER_OR_ASSERTIONS_CHANGED');return {...fault,proof};}});
  need(sources.every(row=>hash(row.copy)===row.sha256),'PRIVATE_XINPUT_SOURCE_CHANGED');need(tools.every(row=>hash(row.path)===row.sha256),'XINPUT_TOOL_BYTES_CHANGED');
  const current=sources.map(row=>({path:row.path,atCapture:row.sha256,current:hash(sourcePath(REPO,row.path))}));const sourceStillCurrent=current.every(row=>row.atCapture===row.current);
  if(!privateSnapshot)need(sourceStillCurrent,'LIVE_XINPUT_SOURCE_CHANGED_DURING_FORMAL_EXECUTION');
  write(path.join(root,'process-ledger.json'),{steps});write(path.join(root,'snapshot-versus-current.json'),{sourceStillCurrent,privateSnapshot,rows:current});
  const files=walk(root),result={schemaVersion:1,id:CASE_ID,status:privateSnapshot?'PRIVATE_SOURCE_SNAPSHOT_VALIDATED_NOT_CURRENT_HEAD':'PASS',passed:!privateSnapshot,kind:'actual-native-physical-feedback-lifecycle-memory-boundaries',positive,negatives,faultScheduling,sourceStillCurrent,privateSnapshot,
   sourceHashes:[...sources.map(({path,sha256})=>({path,sha256})),...files.map(file=>({path:file,sha256:hash(file)}))],toolchain:tools,
   historicalBaseline:{sha256:BASELINE_SHA256,originalBytesPreserved:true,classificationIncludesR103DiscoveryPrerequisite:true,adjacentBeforeAdoptionIsNotFullMatchingPreimage:true,oldProfilesAreHistoricalSourceDerivedFixtures:true,currentSourceFaultsIndependentlyCaughtByOriginalBusinessAssertions:true},
   boundary:{currentProducerAndAssertionsPinnedBeforeExecution:true,originalBusinessAssertionsRetained:true,fixtureByteIdenticalToBeforeTotalBinding:false,actualMsiSourceLeaseOpenFrameWriteBodiesExecuted:true,msiWin32BoundaryControlled:true,realXInputWrites:false,realVibrationLedOrPhysicalActuatorIo:false,hostOrInstallationOrVisibilityOperation:false,noDeviceImportsObserved:true,fullNativeProgramBuilt:false,wholeMainSnapshotStoredForIdentity:true,gameOutputCallbackBound:false,gameFeedbackForwardingClosed:false},
  };write(path.join(root,privateSnapshot?'private-result.json':'result.json'),result);return result;
 }catch(error){write(path.join(root,'failure.json'),{id:CASE_ID,status:'FAIL',error:error.message,code:error.code??null,stageFailure:error.stageFailure??null,stageEvidence:error.stageEvidence??null,steps,faultScheduling,originalFailurePreserved:true,privateSnapshot});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const args=parseArgs(process.argv.slice(2));const result=await runLifecycle(args);console.log(JSON.stringify({id:CASE_ID,status:result.status,positive:result.positive,files:result.files?.length,negatives:result.negatives?.length}));}catch(error){console.error(error.stack??error.message);process.exitCode=2;}
}
