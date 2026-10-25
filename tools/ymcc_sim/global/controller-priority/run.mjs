import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {CASES,MODULE,REPO,moduleSources,compilerArtifacts} from './adapter.mjs';
import {sourcePath,runProcess} from '../runner.mjs';
import {calibrationToolchainPreamble} from '../../../fixtures/hc_reviewed_calibration/toolchain.mjs';
const sha=value=>crypto.createHash('sha256').update(value).digest('hex');
const hash=file=>sha(fs.readFileSync(file));
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const need=(ok,code)=>{if(!ok)throw Error(code);};
function walk(root){return fs.readdirSync(root,{withFileTypes:true}).flatMap(entry=>{
 const file=path.join(root,entry.name);if(entry.isSymbolicLink())throw Error('CONTROLLER_PRIORITY_OUTPUT_LINK');
 return entry.isDirectory()?walk(file):[file];
});}
export function parseArgs(args){const result={};for(let i=0;i<args.length;i++){
 const key=args[i];need(['--test','--evidence-dir'].includes(key)&&args[i+1]&&!args[i+1].startsWith('--'),'ARGUMENT_INVALID');
 need(result[key]===undefined,'DUPLICATE_ARGUMENT');result[key]=args[++i];
 }need(CASES[result['--test']]&&result['--evidence-dir'],'CASE_AND_EVIDENCE_REQUIRED');return{id:result['--test'],evidenceDir:result['--evidence-dir']};}
export function verifyOriginalOutput(kind,text,{negative=false,exitCode=0}={}){
 need(['policy','runtime'].includes(kind),'UNREVIEWED_CONTROLLER_PRIORITY_KIND');
 const tail=kind==='runtime'?' operations=faked-external-effects ledgerReads=private-temp-fixtures':'';
 const pattern=new RegExp('^EXTERNALPAD_'+kind.toUpperCase()+'_SUMMARY passed=(\\d+) total=(\\d+) assertions=(\\d+) hardwareAccess=false productRun=false productionHeader=true'+tail+'\\r?$','gm');
 const rows=[...text.matchAll(pattern)].map(match=>({passed:Number(match[1]),total:Number(match[2]),assertions:Number(match[3])}));
 const minimum=kind==='policy'?19:33;
 need(rows.length>0&&rows.every(row=>Number.isSafeInteger(row.passed)&&row.passed>=0&&row.total>=minimum&&row.passed<=row.total&&row.assertions>=row.total),'ORIGINAL_SUMMARY_MISSING_OR_INVALID');
 if(negative){need(exitCode!==0&&rows.some(row=>row.passed<row.total)&&/^FAIL /m.test(text),'FAULT_NOT_DETECTED_BY_ORIGINAL_ASSERTIONS');}
 else {need(exitCode===0&&rows.length===2&&rows.every(row=>row.passed===row.total)&&!/^FAIL /m.test(text),'ORIGINAL_POSITIVE_NOT_PASS');
  need(new RegExp('^EXTERNALPAD_'+kind.toUpperCase()+'_PASSED .+\\r?$','m').test(text),'ORIGINAL_COMPLETION_RECEIPT_MISSING');}
 return{status:negative?'EXPECTED_REJECTION':'PASS',profiles:rows.length,checks:rows.reduce((n,row)=>n+row.total,0),assertions:rows.reduce((n,row)=>n+row.assertions,0),rows,originalAssertionsUnchanged:true};
}
export async function runControllerPriority(id,evidenceDir,{repoRoot=REPO}={}){
 const spec=CASES[id];need(spec,'CASE_REQUIRED');const root=path.resolve(evidenceDir);
 need(!fs.existsSync(root),'EVIDENCE_DIRECTORY_ALREADY_EXISTS');fs.mkdirSync(root,{recursive:true});
 const sources=[...new Set([...moduleSources(),...spec.sources,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/runner.mjs'])].sort();
 const snapshot=sources.map(relative=>{const file=sourcePath(repoRoot,relative),copy=path.join(root,'input-sources',relative);fs.mkdirSync(path.dirname(copy),{recursive:true});fs.copyFileSync(file,copy);return{path:relative,sha256:hash(file),copy};});
 const toolFiles=compilerArtifacts().map(file=>({path:file,sha256:hash(file)}));need(toolFiles.length>=3,'INSTALLED_MSVC_TOOLCHAIN_REQUIRED');
 const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
 const toolchain=path.join(root,'installed-toolchain.cmd');fs.writeFileSync(toolchain,calibrationToolchainPreamble(vcvars));
 const powershell=process.env.YMCC_SIM_PWSH||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe';
 need(fs.existsSync(powershell),'POWERSHELL_7_REQUIRED');const steps=[];
 async function execute(label,sourceRoot){
  const dir=path.join(root,label);fs.mkdirSync(dir);const privateTemp=path.join(dir,'private-temp');fs.mkdirSync(privateTemp);
  const config=path.join(dir,'config.json'),argsFile=path.join(dir,'arguments.json');
  write(config,{kind:spec.kind,sourceRoot,privateTemp,outputRoot:path.join(dir,'artifacts'),toolchainCmd:toolchain});write(argsFile,[config]);
  const args=['-NoProfile','-NonInteractive','-File',path.join(repoRoot,'tools/ymcc_sim/global/host-process.ps1'),'-ScriptPath',path.join(repoRoot,MODULE,'Run-Original.ps1'),'-ArgumentsPath',argsFile];
  const p=await runProcess(powershell,args,{cwd:repoRoot,env:{...process.env},timeoutMs:100000,outputLimit:2*1024*1024});
  fs.writeFileSync(path.join(dir,'stdout.log'),p.stdout??'');fs.writeFileSync(path.join(dir,'stderr.log'),p.stderr??'');
  steps.push({label,executable:powershell,args,pid:p.pid,exitCode:p.exitCode,signal:p.signal,timedOut:p.timedOut,launchError:p.launchError,outputTruncated:p.outputTruncated,ownedJob:true});
  need(!p.timedOut&&!p.launchError&&!p.signal&&!p.outputTruncated&&Number.isInteger(p.exitCode),'ORIGINAL_PROCESS_INCOMPLETE');
  return verifyOriginalOutput(spec.kind,(p.stdout??'')+'\n'+(p.stderr??''),{negative:label==='fault',exitCode:p.exitCode});
 }
 try{
  const positive=await execute('positive',repoRoot);
  const mirror=path.join(root,'fault-source');for(const relative of spec.sources){const from=path.join(root,'input-sources',relative),to=path.join(mirror,relative);fs.mkdirSync(path.dirname(to),{recursive:true});fs.copyFileSync(from,to);}
  const target=path.join(mirror,spec.fault.path),before=fs.readFileSync(target,'utf8');need(before.split(spec.fault.before).length===2,'ACTUAL_FAULT_ANCHOR_NOT_UNIQUE');
  const changed=before.replace(spec.fault.before,spec.fault.after);fs.writeFileSync(target,changed);
  const fault=await execute('fault',mirror);
  need(hash(path.join(mirror,spec.script))===hash(path.join(root,'input-sources',spec.script)),'ORIGINAL_SCRIPT_CHANGED_IN_FAULT');
  need(hash(path.join(mirror,spec.script.replace('.ps1','.cpp')))===hash(path.join(root,'input-sources',spec.script.replace('.ps1','.cpp'))),'ORIGINAL_ASSERTIONS_CHANGED_IN_FAULT');
  need(snapshot.every(row=>hash(sourcePath(repoRoot,row.path))===row.sha256&&hash(row.copy)===row.sha256),'SOURCE_CHANGED_DURING_RUN');
  need(toolFiles.every(row=>hash(row.path)===row.sha256),'COMPILER_CHANGED_DURING_RUN');
  write(path.join(root,'process-ledger.json'),{steps});write(path.join(root,'fault-proof.json'),{path:spec.fault.path,originalSha256:sha(before),faultSha256:sha(changed),before:spec.fault.before,after:spec.fault.after,sourceDerivedCurrentFault:true,historicalBefore:false,originalScriptAndAssertionsUnchanged:true});
  const outputs=walk(root);const result={schemaVersion:1,id,status:'PASS',passed:true,kind:'external-controller-priority-production-consumer',positive,fault,
   sourceHashes:[...snapshot.map(({path,sha256})=>({path,sha256})),...outputs.map(file=>({path:file,sha256:hash(file)}))],
   executedProductModules:spec.sources.filter(file=>file.startsWith('native/')),
   inputConsumerStateFeedback:{source:'Original fixtures',consumer:'Actual production headers compiled by original unchanged scripts',state:'Original positive and explicit production-source mutation',feedback:'Original native assertions and terminal summaries'},
   toolchain:toolFiles,process:{steps:steps.length,ownedJobs:true,allTerminatedNaturally:steps.every(row=>!row.signal&&!row.timedOut)},
   compatibility:{scope:'Current formal source headers and original fixtures only',physicalHardwareVerified:false},
   boundary:{nativeAppStarted:false,productHostStarted:false,realDeviceEnumeration:false,deviceIo:'Original inert terminals',ledgerReads:spec.kind==='runtime'?'Private temporary fixture files':'none'},
   remainingGaps:['Real native app integration, physical identity/visibility/device return and OS/driver behavior require separate acceptance.','This adapter does not bind the HC unified model catalog or prove all controller capabilities complete.']};
  write(path.join(root,'result.json'),result);return result;
 }catch(error){write(path.join(root,'failure.json'),{status:'FAIL',id,code:error.message,steps,originalFailurePreserved:true});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const args=parseArgs(process.argv.slice(2));const result=await runControllerPriority(args.id,args.evidenceDir);console.log(JSON.stringify({id:result.id,status:result.status,positive:result.positive,fault:result.fault}));}
 catch(error){console.error(error.stack??error.message);process.exitCode=2;}
}
