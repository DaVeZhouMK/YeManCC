import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {fileURLToPath} from 'node:url';
import {CASE_ID,MODULE,REPO,ORIGINAL,moduleSources,installedTools} from './adapter.mjs';
import {sourcePath,runProcess} from '../runner.mjs';
import {calibrationToolchainPreamble} from '../../../fixtures/hc_reviewed_calibration/toolchain.mjs';
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex'),hash=file=>sha(fs.readFileSync(file));
const need=(value,code)=>{if(!value)throw Error(code);};const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
function walk(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(entry=>{need(!entry.isSymbolicLink(),'LEDGER_OUTPUT_LINK_NOT_ADMITTED');const file=path.join(dir,entry.name);return entry.isDirectory()?walk(file):entry.isFile()?[file]:[];});}
export function verifyOriginalLedgerOutput(text,{negative=false,exitCode=0}={}){
 const rows=[...text.matchAll(/^EXTERNALPAD_LEDGER_SUMMARY passed=(\d+) total=(\d+) assertions=(\d+) hardwareAccess=false productRun=false filesystem=private-temp-only\r?$/gm)].map(row=>({passed:Number(row[1]),total:Number(row[2]),assertions:Number(row[3])}));
 need(rows.length>0&&rows.every(row=>row.total===13&&row.assertions>=row.total),'ORIGINAL_LEDGER_SUMMARY_MISSING');
 if(negative)need(exitCode!==0&&rows.some(row=>row.passed<row.total)&&/^FAIL /m.test(text),'ORIGINAL_LEDGER_SOURCE_FAULT_NOT_DETECTED');
 else need(exitCode===0&&rows.length===2&&rows.every(row=>row.passed===row.total)&&/^EXTERNALPAD_LEDGER_PASSED /m.test(text)&&!/^FAIL /m.test(text),'ORIGINAL_LEDGER_TERMINATION_INCOMPLETE');
 return {status:negative?'EXPECTED_REJECTION':'PASS',profiles:rows.length,scenarios:rows.reduce((n,row)=>n+row.total,0),assertions:rows.reduce((n,row)=>n+row.assertions,0),rows};
}
export async function runLedger(evidenceDir,{discoverTools=false}={}){
 const root=path.resolve(evidenceDir);need(!fs.existsSync(root),'FRESH_LEDGER_EVIDENCE_ROOT_REQUIRED');fs.mkdirSync(root,{recursive:true});
 const sourceFiles=[...new Set([...moduleSources().filter(relative=>fs.existsSync(path.join(REPO,relative))),...ORIGINAL,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/ymcc_sim/global/controller-priority/adapter.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])];
 const sources=sourceFiles.map(relative=>{const file=sourcePath(REPO,relative),bytes=fs.readFileSync(file),copy=path.join(root,'input-sources',relative);fs.mkdirSync(path.dirname(copy),{recursive:true});fs.writeFileSync(copy,bytes);return{path:relative,sha256:sha(bytes),copy};});
 const tools=installedTools().map(file=>({path:file,sha256:hash(file)}));
 const powershell=tools.find(row=>/pwsh\.exe$/i.test(row.path)).path,vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
 const toolchain=path.join(root,'installed-toolchain.cmd');fs.writeFileSync(toolchain,calibrationToolchainPreamble(vcvars));
 const steps=[];
 async function execute(label,sourceRoot,{negative=false,boundary=false}={}){
  const dir=path.join(root,label);fs.mkdirSync(dir);const config=path.join(dir,'config.json'),args=path.join(dir,'arguments.json');
  write(config,{caseRoot:root,root:dir,sourceRoot,toolchainCmd:toolchain,boundary});write(args,[config]);
  const processResult=await runProcess(powershell,['-NoLogo','-NoProfile','-NonInteractive','-File',path.join(REPO,'tools/ymcc_sim/global/host-process.ps1'),'-ScriptPath',path.join(REPO,MODULE,'Run-Ledger.ps1'),'-ArgumentsPath',args],{cwd:dir,env:process.env,timeoutMs:110000});
  fs.writeFileSync(path.join(dir,'stdout.log'),processResult.stdout);fs.writeFileSync(path.join(dir,'stderr.log'),processResult.stderr);
  const {stdout,stderr,...safe}=processResult;steps.push({label,...safe,ownedWindowsJob:true});
  need(!processResult.signal&&!processResult.timedOut&&!processResult.launchError&&!processResult.outputTruncated&&Number.isInteger(processResult.exitCode),'LEDGER_ORIGINAL_PROCESS_INCOMPLETE');
  const proof=verifyOriginalLedgerOutput(processResult.stdout+'\n'+processResult.stderr,{negative,exitCode:processResult.exitCode});
  if(!negative)need(processResult.stdout.split(/\r?\n/).filter(line=>line==='PASS child interruption with flushed orphan and old file preserved').length===2,'ACTUAL_OWNED_CHILD_INTERRUPTION_NOT_EXECUTED');
  const before=read(path.join(dir,'path-proof-before.json')),after=read(path.join(dir,'path-proof-after.json'));
  need(path.resolve(before.caseRoot)===root&&path.resolve(before.nativeTempPath)===path.resolve(dir,'private-temp')&&after.nativeTempPath===before.nativeTempPath,'ACTUAL_LEDGER_ROOT_NOT_OWNED');
  need(before.beforeTempFiles.length===0&&after.afterTempFiles.length===0,'ORIGINAL_TEMP_FIXTURES_NOT_CLEANLY_RETURNED');
  const smoke=walk(path.join(dir,'artifacts')).filter(file=>path.basename(file)==='standalone-header.obj');need(smoke.length===1,'ORIGINAL_FIRST_INCLUDE_COMPILE_NOT_OBSERVED');
  if(boundary){
   need(/^LEDGER_BOUNDARY_SUMMARY checks=\d+ snapshots=3 hardwareAccess=false explicitOwnedRoot=true\r?$/m.test(processResult.stdout),'ACTUAL_LEDGER_BEFORE_AFTER_NOT_COMPLETED');
   const snapshots=['before','partial','cleared'].map(name=>({name,path:path.join(dir,'observed-ledger',name+'.json'),value:read(path.join(dir,'observed-ledger',name+'.json'))}));
   need(snapshots[0].value.hiddenPaths.length===2&&snapshots[1].value.hiddenPaths.length===1&&snapshots[2].value.hiddenPaths.length===0,'ACTUAL_LEDGER_SNAPSHOTS_NOT_MATCHING_PARTIAL_RETURN');
   write(path.join(dir,'actual-ledger-before-after.json'),{snapshots:snapshots.map(({name,path,value})=>({name,path,sha256:hash(path),hiddenPaths:value.hiddenPaths,applications:value.applications,cloakChanged:value.cloakChanged}))});
  }
  return proof;
 }
 try{
  const positive=await execute('positive',path.join(root,'input-sources'),{boundary:true});
  const includeFiles=new Set();for(const file of walk(root).filter(file=>/\.log$/i.test(file))){const text=fs.readFileSync(file,'utf8');for(const row of text.matchAll(/^\s*(?:Note: including file:|注意:\s*包含文件:)\s*(.+?)\r?$/gm)){const target=path.resolve(row[1].trim());if(!target.toLowerCase().startsWith(root.toLowerCase()+path.sep))includeFiles.add(target);}}
  need(includeFiles.size>20,'ACTUAL_COMPILER_INCLUDE_CLOSURE_NOT_OBSERVED');
  need([...includeFiles].every(file=>/^C:\\Program Files \(x86\)\\(?:Microsoft Visual Studio\\2022\\BuildTools\\VC\\Tools\\MSVC|Windows Kits\\10)\\/i.test(file)&&fs.statSync(file).isFile()),'UNREVIEWED_COMPILER_INCLUDE_ROOT');
  const discovered=[...includeFiles].sort();write(path.join(root,'observed-tool-inputs.json'),{schemaVersion:1,files:discovered,hashes:discovered.map(path=>({path,sha256:hash(path)})),meaning:'Actual /showIncludes observation. This discovery alone is not a before-and-after pinned acceptance.'});
  if(discoverTools){const discovery={schemaVersion:1,status:'TOOLS_DISCOVERED_NOT_ACCEPTANCE',positive,files:discovered,steps};write(path.join(root,'discovery.json'),discovery);return discovery;}
  need(discovered.every(file=>tools.some(row=>row.path.toLowerCase()===file.toLowerCase())),'ACTUAL_COMPILER_INCLUDE_NOT_PINNED_BEFORE_EXECUTION');
  const faults=[{id:'owner-save-admission',before:'if (debt.owner != debtOwner) return fail(error, "invalid owner");',after:'if (debt.owner.empty()) return fail(error, "invalid owner");',expected:'strict schema, duplicate keys, types and exact owner'},{id:'flush-failure-ignored',before:'if (!FlushFileBuffers(temp.file.handle)) return detail::winFail(error, "flush ledger temp", GetLastError());',after:'if (!FlushFileBuffers(temp.file.handle) && GetLastError() == ERROR_SUCCESS) return detail::winFail(error, "flush ledger temp", GetLastError());',expected:'partial-write, zero-write, flush and rename failures'}];
  const negatives=[];
  for(const fault of faults){const mirror=path.join(root,'fault-source-'+fault.id);for(const input of sources){const target=path.join(mirror,input.path);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(input.copy,target);}const file=path.join(mirror,'native/external_controller_priority_ledger.h'),before=fs.readFileSync(file,'utf8');need(before.split(fault.before).length===2,'LEDGER_PRODUCTION_FAULT_ANCHOR_NOT_UNIQUE');fs.writeFileSync(file,before.replace(fault.before,fault.after));const proof=await execute('negative-'+fault.id,mirror,{negative:true});const originalText=fs.readFileSync(path.join(root,'negative-'+fault.id,'stdout.log'),'utf8')+'\n'+fs.readFileSync(path.join(root,'negative-'+fault.id,'stderr.log'),'utf8');need(originalText.split(/\r?\n/).some(line=>line.startsWith('FAIL '+fault.expected+':')),'WRONG_ORIGINAL_ASSERTION_CAUGHT_SOURCE_FAULT');for(const name of ORIGINAL.filter(name=>name.startsWith('tools/')))need(hash(path.join(mirror,name))===sources.find(row=>row.path===name).sha256,'ORIGINAL_LEDGER_ASSERTIONS_CHANGED_IN_FAULT');negatives.push({...fault,proof,originalScriptAndAssertionsUnmodified:true});}
  need(sources.every(row=>hash(sourcePath(REPO,row.path))===row.sha256&&hash(row.copy)===row.sha256),'LEDGER_SOURCE_CHANGED_DURING_RUN');need(tools.every(row=>hash(row.path)===row.sha256),'LEDGER_INSTALLED_TOOL_CHANGED_DURING_RUN');
  write(path.join(root,'process-ledger.json'),{steps});
  const files=walk(root),result={schemaVersion:1,id:CASE_ID,status:'PASS',passed:true,kind:'actual-private-return-debt-persistence',positive,negatives,
   sourceHashes:[...sources.map(({path,sha256})=>({path,sha256})),...files.map(file=>({path:file,sha256:hash(file)}))],toolchain:tools,
   childInterruption:{source:'Original self-executable --interrupt-save branch',expectedExitCode:73,privateRootPrefix:'ymcc-externalpad-ledger-fixture-',marker:'fixture-only.marker=externalpad-test-only',sameCompiledExecutableAsParent:true,sourceBranchAndOldFileAssertionsActuallyPassed:true,noNameBasedTermination:true},
   boundary:{originalScriptsAndProductSourceUnmodified:true,actualWindowsFilesystem:true,allRootsInOwnedCase:true,realDeviceOrRegistryIo:false,productStarted:false,physicalVisibilityReturnValidated:false,driverOrHardwareRestoreValidated:false},
  };write(path.join(root,'result.json'),result);return result;
 }catch(error){write(path.join(root,'failure.json'),{schemaVersion:1,id:CASE_ID,status:'FAIL',error:error.message,steps,originalFailurePreserved:true});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const args=process.argv.slice(2);need(args.length===2||args.length===3,'ARGUMENT_INVALID');need(args[0]==='--evidence-dir'&&(!args[2]||args[2]==='--discover-tools'),'ARGUMENT_INVALID');
 runLedger(args[1],{discoverTools:args[2]==='--discover-tools'}).then(result=>console.log(JSON.stringify({status:result.status,positive:result.positive,files:result.files?.length,negatives:result.negatives?.length})),error=>{console.error(error.stack??error.message);process.exitCode=2;});
}
