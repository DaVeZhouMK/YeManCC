import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {REPO,MODULE,CASE_ID,PRODUCER,PRODUCT,moduleSources,productSources,compilerTools} from './adapter.mjs';
import {sourcePath,runProcess} from '../runner.mjs';
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const hash=file=>sha(fs.readFileSync(file));
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const need=(condition,code)=>{if(!condition)throw Error(code);};
function outputs(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(entry=>{need(!entry.isSymbolicLink(),'CALIBRATION_OUTPUT_LINK');const file=path.join(dir,entry.name);return entry.isDirectory()?outputs(file):entry.isFile()?[file]:[];});}
export async function runCalibrationSession(evidenceRoot,{repoRoot=REPO}={}){
 const root=path.resolve(evidenceRoot);need(!fs.existsSync(root),'FRESH_EVIDENCE_DIRECTORY_REQUIRED');fs.mkdirSync(root,{recursive:true});
 const sources=[...new Set([...moduleSources(),...productSources(),'tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])];
 const snapshot=sources.map(relative=>{const bytes=fs.readFileSync(sourcePath(repoRoot,relative)),copy=path.join(root,'input-sources',relative);fs.mkdirSync(path.dirname(copy),{recursive:true});fs.writeFileSync(copy,bytes);return{path:relative,sha256:sha(bytes),copy};});
 const tools=compilerTools(repoRoot).map(file=>({path:file,sha256:hash(file)}));
 const powershell=tools.find(row=>/pwsh\.exe$/i.test(row.path)).path,esbuildMain=tools.find(row=>/esbuild[\\/]lib[\\/]main\.js$/i.test(row.path)).path,esbuildBinary=tools.find(row=>/esbuild\.exe$/i.test(row.path)).path;
 const steps=[];
 async function execute(label,sourceRoot,expectedFailure=null){
  const dir=path.join(root,label);fs.mkdirSync(dir);const config=path.join(dir,'config.json'),argumentsPath=path.join(dir,'arguments.json');
  write(config,{root:dir,sourceRoot,node:process.execPath,worker:path.join(repoRoot,MODULE,'worker.mjs'),producer:PRODUCER,supplemental:MODULE+'/session-exit-selftest.ts',expectedFailure,esbuildMain,esbuildBinary});write(argumentsPath,[config]);
  const env={...process.env};for(const key of ['NODE_OPTIONS','NODE_PATH','ESBUILD_BINARY_PATH'])delete env[key];
  const execution=await runProcess(powershell,['-NoLogo','-NoProfile','-NonInteractive','-File',path.join(repoRoot,'tools/ymcc_sim/global/host-process.ps1'),'-ScriptPath',path.join(repoRoot,MODULE,'Run-Session.ps1'),'-ArgumentsPath',argumentsPath],{cwd:dir,env,timeoutMs:35000});
  fs.writeFileSync(path.join(dir,'job-stdout.log'),execution.stdout);fs.writeFileSync(path.join(dir,'job-stderr.log'),execution.stderr);
  const {stdout,stderr,...savedProcess}=execution;steps.push({label,...savedProcess,ownedWindowsJob:true});
  need(execution.exitCode===0&&!execution.signal&&!execution.timedOut&&!execution.launchError&&!execution.outputTruncated,'OWNED_SESSION_PROCESS_NOT_COMPLETE:'+label);
  const result=read(path.join(dir,'worker-result.json'));need(result.status==='PASS'&&result.original.exitCode===(expectedFailure?1:0),'ORIGINAL_SESSION_RESULT_NOT_COMPLETE:'+label);
  for(const build of result.builds){need(build.compileSucceeded&&hash(build.bundle)===build.bundleSha256,'ACTUAL_BUNDLE_CHANGED');for(const input of build.inputs)need(hash(path.join(sourceRoot,input.path))===input.sha256,'COMPILER_INPUT_CHANGED');}
  need(result.observedCompilerModules.every(row=>tools.some(tool=>tool.path===row.path&&tool.sha256===row.sha256)),'UNREGISTERED_COMPILER_TOOL_MODULE');
  return result;
 }
 try{
  const positive=await execute('positive',path.join(root,'input-sources'));
  const faults=[
   {id:'old-power-generation',before:'expected.powerGeneration === sample.powerGeneration && expected.providerId === sample.providerId',after:'true && expected.providerId === sample.providerId',expected:'T11 old power generation was accepted'},
   {id:'skip-lock-safe-zero',before:"gyroContributionAllowed: this.state === 'manual-bound' && this.calibration !== null,",after:"gyroContributionAllowed: this.state === 'manual-bound' || this.state === 'lock-requested',",expected:'T11 confidence==1 candidate skipped the safe-zero lock barrier'},
  ];
  const negatives=[];
  for(const fault of faults){const mirror=path.join(root,'fault-source-'+fault.id);for(const source of snapshot){const target=path.join(mirror,source.path);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(source.copy,target);}
   const product=path.join(mirror,PRODUCT),before=fs.readFileSync(product,'utf8');need(before.split(fault.before).length===2,'PRODUCTION_FAULT_ANCHOR_NOT_UNIQUE');fs.writeFileSync(product,before.replace(fault.before,fault.after));
   const negative=await execute('negative-'+fault.id,mirror,fault.expected);
   need(hash(path.join(mirror,PRODUCER))===snapshot.find(row=>row.path===PRODUCER).sha256,'ORIGINAL_ASSERTIONS_CHANGED_IN_FAULT');
   negatives.push({id:fault.id,product:PRODUCT,before:fault.before,after:fault.after,expectedOriginalFailure:fault.expected,compileSucceeded:negative.builds[0].compileSucceeded,originalExitCode:negative.original.exitCode,status:'EXPECTED_REJECTION',originalAssertionsUnmodified:true});
  }
  need(snapshot.every(row=>hash(sourcePath(repoRoot,row.path))===row.sha256&&hash(row.copy)===row.sha256),'SOURCE_CHANGED_DURING_CALIBRATION_SESSION');
  need(tools.every(row=>hash(row.path)===row.sha256),'CALIBRATION_COMPILER_OR_RUNTIME_CHANGED');
  write(path.join(root,'process-ledger.json'),{steps});write(path.join(root,'actual-source-faults.json'),{negatives});
  const files=outputs(root),result={schemaVersion:1,id:CASE_ID,status:'PASS',passed:true,kind:'original-gyro-calibration-session-policy',positive:{originalSessions:8,originalReportStatus:positive.originalReportStatus,originalEvidencePath:positive.originalEvidencePath,originalEvidenceSha256:positive.originalEvidenceSha256,exit:positive.supplemental,metafileInputs:positive.builds.map(row=>({entry:row.entry,inputs:row.inputs})),fixedRelativeOutputConfinedToOwnedCase:true},negatives,
   sourceHashes:[...snapshot.map(({path,sha256})=>({path,sha256})),...files.map(file=>({path:file,sha256:hash(file)}))],toolchain:tools,
   boundary:{originalAssertionsUnmodified:true,productSourceUnmodified:true,ownedWindowsJobs:steps.length,osOrHardwareIo:false,realGamepadMotionAlgorithmExecuted:false,physicalImuOrOemProviderValidated:false,completeControllerMotionValidated:false,originalPhysicalBaseInputFlagIsOnlyDeclaration:true},
   remainingGaps:['Only the formal calibration-session policy executes. GamepadMotion stillness/confidence values are fixture observations.','The original physicalBaseInputOutsideGyroGate assertion flag does not prove actual native physical input isolation.','OEM/MSI providers, real IMU, native calibration algorithm and full controller motion remain separate.']};
  write(path.join(root,'result.json'),result);return result;
 }catch(error){write(path.join(root,'failure.json'),{status:'FAIL',id:CASE_ID,code:error.message,steps,originalFailuresPreserved:true});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 if(process.argv.length!==4||process.argv[2]!=='--evidence-dir')throw Error('--evidence-dir <new absolute directory> required');
 runCalibrationSession(process.argv[3]).then(result=>console.log(JSON.stringify({id:result.id,status:result.status,positive:result.positive,negatives:result.negatives})),error=>{console.error(error.stack??error.message);process.exitCode=2;});
}
