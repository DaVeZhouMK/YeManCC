import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {fileURLToPath} from 'node:url';
import {MODULE,REPO,PRODUCT,SHARED,INPUTS,getControllerPortCycleAdapters} from './adapter.mjs';
import {runProcess,sourcePath} from '../runner.mjs';
const sha=data=>crypto.createHash('sha256').update(data).digest('hex'),hash=file=>sha(fs.readFileSync(file));
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const need=(ok,code)=>{if(!ok)throw Error(code);};
const inside=(root,file)=>{const rel=path.relative(root,file);return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));};
const walk=root=>fs.readdirSync(root,{withFileTypes:true}).flatMap(entry=>{const file=path.join(root,entry.name);need(!entry.isSymbolicLink(),'OUTPUT_LINK_REFUSED');return entry.isDirectory()?walk(file):[file];});
// The original SelfTest emits only a count for passing checks, not their names.
const ORIGINAL_CHECKS=403;
const ADMISSION_FAILURES=['unknown board result','unknown board exact counts'];
export function verifyOriginal(stdout,{negative=false,exitCode=0,stderr='',originalScript}={}){
 need(Number.isInteger(exitCode),'ORIGINAL_EXIT_MISSING');
 const lines=stdout.trimEnd().split(/\r?\n/),terminal=lines.at(-1);
 const candidates=lines.filter(line=>line.trimStart().startsWith('{'));
 need(candidates.length===1&&terminal===candidates[0],'ORIGINAL_PORT_CYCLE_REPORT_MISSING');
 let row;try{row=JSON.parse(terminal);}catch{throw Error('ORIGINAL_PORT_CYCLE_REPORT_INVALID');}
 const expected=negative?ADMISSION_FAILURES:[];
 need(row?.cases===ORIGINAL_CHECKS&&row.failures===expected.length&&Array.isArray(row.failedCases)&&row.failedCases.length===row.failures,'ORIGINAL_PORT_CYCLE_COUNTS_INVALID');
 need(expected.every(name=>row.failedCases.includes(name)),'ORIGINAL_PORT_CYCLE_FAULT_MISMATCH');
 need(row.realHardwareAccess===false&&row.noTargetCreation===true&&row.maxLocationReads===2&&row.maxPreparations===1&&row.maxCycles===1,'ORIGINAL_PORT_CYCLE_BOUNDARY_MISMATCH');
 if(negative){
  const errors=stderr.replace(/\x1b\[[0-9;]*m/g,'').trim().split(/\r?\n/);
  need(exitCode===1&&originalScript&&errors[0].startsWith(`Exception: ${originalScript}:`)&&
   errors.at(-1).trimStart()==='| Injected self-test failed with exit code 3','ORIGINAL_PORT_CYCLE_FAULT_TERMINAL_MISMATCH');
 }else need(exitCode===0&&stderr.trim()==='','ORIGINAL_PORT_CYCLE_UNEXPECTED_RESULT');
 return{...row,status:negative?'EXPECTED_REJECTION':'PASS',originalAssertionsUnchanged:true};
}
export async function runPortCycle(evidenceDir,{repoRoot=REPO}={}){
 const root=path.resolve(evidenceDir);need(!fs.existsSync(root),'EVIDENCE_DIRECTORY_ALREADY_EXISTS');fs.mkdirSync(root,{recursive:true});
 const snapshots=INPUTS.map(relative=>{const file=sourcePath(repoRoot,relative),copy=path.join(root,'input-sources',relative);fs.mkdirSync(path.dirname(copy),{recursive:true});fs.copyFileSync(file,copy);return{path:relative,sha256:hash(file),copy};});
 const artifacts=getControllerPortCycleAdapters(repoRoot)[0].artifactInputs.map(file=>({path:file,sha256:hash(file)}));
 const pwsh=process.env.YMCC_SIM_PWSH||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe';
 const steps=[];
 async function execute(label,sourceRoot){
  const dir=path.join(root,label);fs.mkdirSync(dir);const temp=path.join(dir,'private-temp');fs.mkdirSync(temp);
  const config=path.join(dir,'config.json'),argumentFile=path.join(dir,'arguments.json');write(config,{sourceRoot});write(argumentFile,[config]);
  const p=await runProcess(pwsh,['-NoProfile','-NonInteractive','-File',path.join(repoRoot,'tools/ymcc_sim/global/host-process.ps1'),'-ScriptPath',path.join(repoRoot,MODULE,'Run-Original.ps1'),'-ArgumentsPath',argumentFile],{cwd:sourceRoot,env:{...process.env,TEMP:temp,TMP:temp,DOTNET_SKIP_FIRST_TIME_EXPERIENCE:'1',DOTNET_CLI_TELEMETRY_OPTOUT:'1'},timeoutMs:100000});
  fs.writeFileSync(path.join(dir,'stdout.log'),p.stdout??'');fs.writeFileSync(path.join(dir,'stderr.log'),p.stderr??'');
  steps.push({label,pid:p.pid,exitCode:p.exitCode,signal:p.signal,timedOut:p.timedOut,launchError:p.launchError,outputTruncated:p.outputTruncated,ownedJob:true});
  need(!p.timedOut&&!p.launchError&&!p.signal&&!p.outputTruncated,'ORIGINAL_PROCESS_INCOMPLETE');
  const original=verifyOriginal(p.stdout??'',{negative:label==='fault',exitCode:p.exitCode,stderr:p.stderr??'',
   originalScript:path.join(sourceRoot,'tools/external_controller_port_cycle_selftest.ps1')});
  const builds=walk(temp).filter(file=>/external_controller_port_cycle_selftest\.(?:dll|pdb)$/.test(file));
  need(builds.some(file=>file.endsWith('.dll'))&&builds.some(file=>file.endsWith('.pdb')),'ACTUAL_PORT_CYCLE_BUILD_OUTPUT_MISSING');
  need(builds.every(file=>inside(dir,file)),'BUILD_OUTPUT_OUTSIDE_CASE');
  return{...original,compiledOutputs:builds.map(file=>({path:file,sha256:hash(file)}))};
 }
 try{
  const positive=await execute('positive',repoRoot);
  const mirror=path.join(root,'fault-source');for(const relative of PRODUCT){const to=path.join(mirror,relative);fs.mkdirSync(path.dirname(to),{recursive:true});fs.copyFileSync(path.join(root,'input-sources',relative),to);}
  fs.mkdirSync(path.dirname(path.join(mirror,SHARED)),{recursive:true});fs.copyFileSync(path.join(repoRoot,SHARED),path.join(mirror,SHARED));
  const file=path.join(mirror,'InputHost/ExternalControllerPortCycle.cs'),before=fs.readFileSync(file,'utf8');
  const anchor='if (!allowed) return UnknownBoard;',replacement='if (!allowed && string.IsNullOrEmpty(root)) return UnknownBoard;';
  need(before.split(anchor).length===2,'MODEL_ADMISSION_FAULT_ANCHOR_NOT_UNIQUE');const after=before.replace(anchor,replacement);fs.writeFileSync(file,after);
  const fault=await execute('fault',mirror);
  need(after.replace(replacement,anchor)===before,'SELFTEST_ASSERTIONS_CHANGED');
  for(const relative of PRODUCT.filter(file=>file.startsWith('tools/')))need(hash(path.join(mirror,relative))===hash(path.join(root,'input-sources',relative)),'ORIGINAL_ENTRY_OR_SCRIPT_CHANGED');
  need(snapshots.every(row=>hash(sourcePath(repoRoot,row.path))===row.sha256&&hash(row.copy)===row.sha256),'SOURCE_CHANGED_DURING_RUN');
  need(artifacts.every(row=>hash(row.path)===row.sha256),'TOOLCHAIN_CHANGED_DURING_RUN');
  write(path.join(root,'process-ledger.json'),{steps});write(path.join(root,'fault-proof.json'),{path:'InputHost/ExternalControllerPortCycle.cs',beforeSha256:sha(before),afterSha256:sha(after),before:anchor,after:replacement,originalSelftestBodyUnchanged:true,sourceDerivedFault:true,historicalBefore:false});
  const files=walk(root);const result={schemaVersion:1,id:'controller.external-port-cycle',status:'PASS',passed:true,positive,fault,
   sourceHashes:[...snapshots.map(({path,sha256})=>({path,sha256})),...files.map(file=>({path:file,sha256:hash(file)}))],
   executedProductModules:['InputHost/ExternalControllerPortCycle.cs: ExecuteVerified/argument/model/USB/location/owner checks through original SelfTest'],
   inputConsumerStateFeedback:{input:'Original SelfTest cases',consumer:'Actual production ExecuteVerified with injected alive/knownBoard/locations/prepare',state:'Original cases and explicit real source model-admission fault',feedback:'Original JSON case count and exact failed case list'},
   boundary:{actualProductMain:false,actualTargetCreation:false,realHardwareAccess:false,parentProcessOps:'Injected original SelfTest delegates',temporaryOutputsInsideCase:true},
   toolchain:{artifactCount:artifacts.length,identities:artifacts},remainingGaps:['Real USB topology, PnP port refresh, parent process handles and actual device reconnection remain separately unverified.','Unified HC controller catalog binding is not implemented by this regression adapter.']};
  write(path.join(root,'result.json'),result);return result;
 }catch(error){write(path.join(root,'failure.json'),{status:'FAIL',code:error.message,steps,originalFailurePreserved:true});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const args=process.argv.slice(2);need(args.length===2&&args[0]==='--evidence-dir'&&args[1],'EVIDENCE_ARGUMENT_REQUIRED');const result=await runPortCycle(args[1]);console.log(JSON.stringify({id:result.id,status:result.status,cases:result.positive.cases,faultCases:result.fault.failedCases}));}
 catch(error){console.error(error.stack??error.message);process.exitCode=2;}
}
