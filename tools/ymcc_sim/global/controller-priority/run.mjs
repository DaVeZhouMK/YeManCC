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
const lines=text=>text.replace(/\r\n/g,'\n').replace(/\n$/,'').split('\n');
const equal=(a,b)=>a.length===b.length&&a.every((value,i)=>value===b[i]);
const profileNames=(kind,negative)=>negative&&kind==='policy'?['release']:['release','checked'];
// Only the endpoints attributed to the existing source mutations are enumerated.
const faultAssertions={
 policy:new Map([
  ['ready rejects every missing evidence field and invalid slot','!ep::ready(rejected)'],
  ['no candidate and mixed unknown virtual nonfresh stay idle','!r.available'],
  ['selected nonfresh virtual unknown count as absence','!r.available'],
 ]),
 runtime:new Map([
  ['R2 source publication blocks on consumer mutex and rechecks context inside lock','accepted == (scenario == 0)'],
 ]),
};
export function originalCaseNames(cpp){
 // The two original fixtures declare each scene as a literal run("...", [] { ... }).
 const names=[...cpp.matchAll(/^[\t ]*run\("([^"\\\r\n]+)", \[\] \{/gm)].map(match=>match[1]);
 need(names.length>0&&new Set(names).size===names.length,'ORIGINAL_CASE_DECLARATIONS_INVALID');
 return names;
}
export function readOriginalArtifacts(artifactsRoot){
 const dirs=fs.readdirSync(artifactsRoot,{withFileTypes:true});
 need(dirs.length===1&&dirs[0].isDirectory()&&/^standalone-[a-f0-9]{32}$/.test(dirs[0].name),'ORIGINAL_STANDALONE_DIRECTORY_INVALID');
 const dir=path.join(artifactsRoot,dirs[0].name),entries=fs.readdirSync(dir,{withFileTypes:true});
 const file=(name)=>{need(entries.some(entry=>entry.name===name&&entry.isFile()),'ORIGINAL_ARTIFACT_MISSING:'+name);return path.join(dir,name);};
 const profiles=new Map(entries.filter(entry=>entry.name.endsWith('-run.log')).map(entry=>{
  const name=entry.name.slice(0,-'-run.log'.length);
  return[name,{log:fs.readFileSync(file(entry.name),'utf8'),binarySha256:hash(file(name+'.exe'))}];
 }));
 const receiptFile=path.join(dir,'verification-summary.txt');
 return{profiles,receiptFile,receipt:entries.some(entry=>entry.name==='verification-summary.txt')?fs.readFileSync(file('verification-summary.txt'),'utf8'):null};
}
export function verifyOriginalOutput(kind,{stdout,stderr,caseNames,sourcePins,profiles,receiptFile,receipt,negative=false,exitCode=0}){
 need(kind==='policy'||kind==='runtime','UNREVIEWED_CONTROLLER_PRIORITY_KIND');
 const prefix='EXTERNALPAD_'+kind.toUpperCase();
 const tail=kind==='runtime'?' operations=faked-external-effects ledgerReads=private-temp-fixtures':'';
 const summaryPattern=new RegExp('^'+prefix+'_SUMMARY passed=(\\d+) total=(\\d+) assertions=(\\d+) hardwareAccess=false productRun=false productionHeader=true'+tail+'$');
 const failures=negative?faultAssertions[kind]:new Map(),expected=new Set(caseNames);
 need(expected.size>0&&expected.size===caseNames.length&&[...failures.keys()].every(name=>expected.has(name)),'ORIGINAL_CASE_DECLARATIONS_INVALID');
 const names=profileNames(kind,negative);
 need(profiles.size===names.length&&names.every(name=>profiles.has(name)),'ORIGINAL_PROFILE_SET_MISMATCH');
 const output=[],profileReceipt=[];
 const rows=names.map(profile=>{
  const {log,binarySha256}=profiles.get(profile),seen=new Set(),failed=[];
  let summary;
  for(const line of lines(log)){
   output.push(line);
   if(line.startsWith(prefix+'_SUMMARY ')){
    need(!summary&&summaryPattern.test(line),'ORIGINAL_SUMMARY_MISSING_OR_INVALID:'+profile);
    summary=line;continue;
   }
   if(!summary&&kind==='runtime'&&line.startsWith('R53_PRIVATE_LEDGER '))continue;
   const ok=line.startsWith('PASS '),rejected=/^FAIL (.+): line [1-9]\d*: (.+)$/.exec(line);
   const name=ok?line.slice(5):rejected?.[1];
   need(expected.has(name)&&!seen.has(name)&&(!summary||kind==='runtime'&&!ok),'ORIGINAL_CASE_SET_MISMATCH:'+profile+':'+line);
   need(ok?!failures.has(name):failures.get(name)===rejected[2],'ORIGINAL_CASE_OUTCOME_MISMATCH:'+profile+':'+name);
   seen.add(name);if(!ok)failed.push(name);
  }
  need(seen.size===expected.size,'ORIGINAL_CASE_SET_MISMATCH:'+profile);
  need(summary,'ORIGINAL_SUMMARY_MISSING_OR_INVALID:'+profile);
  const [passed,total,assertions]=summaryPattern.exec(summary).slice(1).map(Number);
  need(total===seen.size&&passed===total-failed.length&&Number.isSafeInteger(assertions)&&assertions>=total,'ORIGINAL_SUMMARY_MISSING_OR_INVALID:'+profile);
  const runtimeExit=kind==='runtime'?` exitCode=${negative?1:0}`:'';
  profileReceipt.push(`profile=${profile}${runtimeExit} ${summary} binarySHA256=${binarySha256.toUpperCase()}`);
  return{profile,passed,total,assertions,caseNames:[...seen].sort(),failedCases:failed.sort()};
 });
 if(kind==='runtime'||!negative){
  const pin=(key,{path,sha256})=>`${key}=${sha256.toUpperCase()} path=${path}`;
  const expectedReceipt=kind==='runtime'
   ?[...sourcePins.map(row=>pin('sourceSHA256',row)),...profileReceipt]
   :[pin('productionHeaderSHA256',sourcePins[2]),...profileReceipt,`fixtureSHA256=${sourcePins[1].sha256.toUpperCase()}`];
  need(receipt!==null&&equal(lines(receipt),expectedReceipt),'ORIGINAL_RECEIPT_MISMATCH');
  if(kind==='runtime')output.push(`${prefix}_RECEIPT ${receiptFile}`);
  if(!negative)output.push(`${prefix}_PASSED ${receiptFile}`);
 }else need(receipt===null,'UNEXPECTED_POLICY_FAULT_RECEIPT');
 const [guard,...reported]=lines(stdout);
 need(/^YMCC_SIM_OWNED_JOB childPid=[1-9]\d*$/.test(guard)&&equal(reported,output),'ORIGINAL_STDOUT_MISMATCH');
 if(negative){
  const errors=lines(stderr.replace(/\x1b\[[0-9;]*m/g,'').trim());
  const expectedError=kind==='policy'?'Policy selftest failed: release (exit 1)':'Runtime regressions failed or production changed during verification. Failures remain failures; see the receipt.';
  need(exitCode===1&&errors[0].startsWith(`Exception: ${sourcePins[0].path}:`)&&errors.at(-1).endsWith('| '+expectedError),'ORIGINAL_FAULT_TERMINAL_MISMATCH');
 }else need(exitCode===0&&stderr.trim()==='','ORIGINAL_POSITIVE_NOT_PASS');
 return{status:negative?'EXPECTED_REJECTION':'PASS',profiles:rows.length,checks:rows.reduce((n,row)=>n+row.total,0),assertions:rows.reduce((n,row)=>n+row.assertions,0),rows,originalAssertionsUnchanged:true};
}
export async function runControllerPriority(id,evidenceDir,{repoRoot=REPO}={}){
 const spec=CASES[id];need(spec,'CASE_REQUIRED');const root=path.resolve(evidenceDir);
 need(!fs.existsSync(root),'EVIDENCE_DIRECTORY_ALREADY_EXISTS');fs.mkdirSync(root,{recursive:true});
 const sources=[...new Set([...moduleSources(),...spec.sources,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/runner.mjs'])].sort();
 const snapshot=sources.map(relative=>{const file=sourcePath(repoRoot,relative),copy=path.join(root,'input-sources',relative);fs.mkdirSync(path.dirname(copy),{recursive:true});fs.copyFileSync(file,copy);return{path:relative,sha256:hash(file),copy};});
 const caseNames=originalCaseNames(fs.readFileSync(path.join(root,'input-sources',spec.script.replace('.ps1','.cpp')),'utf8'));
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
  const sourcePins=spec.sources.map(relative=>{const file=path.join(sourceRoot,relative);return{path:file,sha256:hash(file)};});
  return verifyOriginalOutput(spec.kind,{...readOriginalArtifacts(path.join(dir,'artifacts')),caseNames,sourcePins,
   stdout:p.stdout??'',stderr:p.stderr??'',negative:label==='fault',exitCode:p.exitCode});
 }
 try{
  need([...faultAssertions[spec.kind].keys()].every(name=>caseNames.includes(name)),'ORIGINAL_FAULT_CASE_DECLARATION_MISSING');
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
