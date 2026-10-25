import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {createRequire} from 'node:module';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {REPO,MODULE,SPECS,MODULE_INPUTS} from './adapter.mjs';
import {runRetiredChannel} from './retired-channel.mjs';
const sha=data=>crypto.createHash('sha256').update(data).digest('hex');
const hash=file=>sha(fs.readFileSync(file));
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const need=(ok,code)=>{if(!ok)throw Error(code);};
const inside=(root,p)=>{const r=path.relative(root,p);return r===''||(!r.startsWith('..'+path.sep)&&r!=='..'&&!path.isAbsolute(r));};
export function evidencePath(value){need(typeof value==='string'&&path.isAbsolute(value),'PLUGIN_UPDATE_EVIDENCE_ABSOLUTE');const p=path.resolve(value);need(!inside(REPO,p),'PLUGIN_UPDATE_EVIDENCE_SOURCE_REPO');need(inside('D:/Codex',p)||inside('G:/YeManCC-Work/Build',p)||inside('G:/YeManCC-Work/Testing/YMCC/Evidence',p)||inside('G:/YeManCC-Work/Testing/YMCC/Workspace',p),'PLUGIN_UPDATE_EVIDENCE_WORKSPACE');need(!fs.existsSync(p),'PLUGIN_UPDATE_FRESH_EVIDENCE_REQUIRED');return p;}
export function parseArgs(args){need(args.length===4&&args[0]==='--case'&&Object.hasOwn(SPECS,args[1])&&args[2]==='--evidence-dir','PLUGIN_UPDATE_ARGUMENTS_INVALID');return {id:args[1],root:evidencePath(args[3])};}
function sourceFile(root,rel){need(typeof rel==='string'&&!path.isAbsolute(rel)&&!rel.split(/[\\/]/).includes('..'),'PLUGIN_UPDATE_SOURCE_PATH_INVALID');const p=fs.realpathSync(path.join(root,rel));need(inside(fs.realpathSync(root),p),'PLUGIN_UPDATE_SOURCE_ESCAPE');return p;}
function actualImports(meta,cwd,root){const files=Object.keys(meta.inputs).map(x=>path.resolve(cwd,x));need(files.every(x=>inside(root,x)),'PLUGIN_UPDATE_UNREVIEWED_EXTERNAL_SOURCE');need(Object.values(meta.outputs).every(x=>!x.imports?.some(y=>y.external)),'PLUGIN_UPDATE_UNREVIEWED_EXTERNAL_IMPORT');return [...new Set(files.map(x=>path.relative(root,x).replaceAll('\\','/')))].sort();}
export function verifyOriginal(raw,spec,{negative=false}={}){need(!spec.retirement,'PLUGIN_UPDATE_RETIRED_CONTRACT_CANNOT_PASS');
 if(spec.format==='ymcc-only-original-string-cases'){
  need(!negative,'PLUGIN_UPDATE_YMCC_NEGATIVE_REQUIRES_OBSERVED_ASSERTION');
  need(raw?.passed===true&&raw.installations===0&&raw.realSteam===false,'PLUGIN_UPDATE_YMCC_BOUNDARY_OR_RESULT');
  need(Array.isArray(raw.results)&&raw.cases===raw.results.length&&new Set(raw.results).size===raw.results.length&&JSON.stringify(raw.results)===JSON.stringify(spec.expectedNames),'PLUGIN_UPDATE_YMCC_ORIGINAL_CASE_REMOVED_OR_CHANGED');
  return {checks:raw.cases,passedNames:raw.results,failedNames:[]};
 }
 need(raw&&Array.isArray(raw.results)&&raw.results.length===raw.cases,'PLUGIN_UPDATE_ORIGINAL_REPORT_SHAPE');const passed=row=>row.passed===true||row.status==='passed',names=raw.results.map(x=>x.name);need(new Set(names).size===names.length&&spec.expectedNames.every(x=>names.includes(x)),'PLUGIN_UPDATE_ORIGINAL_CASE_REMOVED');
 if(spec.report==='UPDATER-SERVICE.json')need(raw.productionService===true&&raw.productionClient===true&&raw.liveNativeInstallation===false&&raw.steamUIActions===0,'PLUGIN_UPDATE_SERVICE_BOUNDARY');
 else need(raw.actualInstallations===0&&raw.fixtures===true,'PLUGIN_UPDATE_NATIVE_BOUNDARY');
 if(negative)need(raw.status==='failed'&&raw.results.some(x=>x.name===spec.fault.expectedFailedCase&&!passed(x)),'PLUGIN_UPDATE_EXPECTED_ASSERTION_NOT_REACHED');
 else need(raw.status==='passed'&&raw.results.every(passed),'PLUGIN_UPDATE_ORIGINAL_ASSERTION_FAILED');return {checks:raw.cases,failedNames:raw.results.filter(x=>!passed(x)).map(x=>x.name)};
}
export function verifyYmccAssertionFailure(failure,spec,fault,scriptFile){
 need(spec.format==='ymcc-only-original-string-cases'&&!spec.retirement,'PLUGIN_UPDATE_YMCC_ASSERTION_SCOPE');
 need(failure?.schemaVersion===1&&failure.observedBy==='uncaughtExceptionMonitor'&&failure.exceptionUnmodified===true&&failure.name==='AssertionError'&&failure.code==='ERR_ASSERTION','PLUGIN_UPDATE_YMCC_EXPECTED_ASSERTION_NOT_OBSERVED');
 need(path.resolve(failure.script)===path.resolve(scriptFile)&&failure.scriptSha256===hash(scriptFile),'PLUGIN_UPDATE_YMCC_ASSERTION_SCRIPT_CHANGED');
 need(spec.expectedNames.includes(fault.expectedFailedCase),'PLUGIN_UPDATE_YMCC_EXPECTED_CASE_UNDECLARED');
 const lines=fs.readFileSync(scriptFile,'utf8').split(/\r?\n/),matching=lines.flatMap((line,index)=>line.includes('check(')&&line.includes(fault.expectedFailedCase)?[index+1]:[]);
 need(matching.length===1&&failure.stack.includes(pathToFileURL(scriptFile).href+':'+matching[0]+':'),'PLUGIN_UPDATE_YMCC_WRONG_ORIGINAL_ASSERTION_FRAME');
 need(JSON.stringify(failure.assertion)===JSON.stringify(fault.assertionSignature),'PLUGIN_UPDATE_YMCC_WRONG_ASSERTION_SIGNATURE');
 return {checks:1,namedAssertionDetections:1,failedNames:[fault.expectedFailedCase],originalAssertionLine:matching[0],observedExceptionCode:failure.code,exceptionUnmodified:true};
}
function walk(root){const files=[];function visit(dir){for(const e of fs.readdirSync(dir,{withFileTypes:true})){if(e.name==='node_modules'&&e.isDirectory())continue;const p=path.join(dir,e.name);if(e.isSymbolicLink()){need(e.name==='esbuild'&&path.basename(dir)==='node_modules','PLUGIN_UPDATE_OUTPUT_REPARSE');continue;}if(e.isDirectory())visit(p);else if(e.isFile())files.push(p);else throw Error('PLUGIN_UPDATE_OUTPUT_NONFILE');need(files.length<=3000,'PLUGIN_UPDATE_OUTPUT_LIMIT');}}visit(root);return files.sort();}
export async function runCase(id,root){const spec=SPECS[id];need(spec,'PLUGIN_UPDATE_CASE_UNKNOWN');const tool=read(path.join(REPO,MODULE,'toolchain-inputs.json'));need(tool.schemaVersion===1&&tool.files.length===5,'PLUGIN_UPDATE_TOOLCHAIN_INVALID');for(const r of tool.files)need(hash(r.path)===r.sha256,'PLUGIN_UPDATE_TOOLCHAIN_CHANGED');need(fs.realpathSync(process.execPath)===fs.realpathSync(tool.nodeExecutable),'PLUGIN_UPDATE_NODE_IDENTITY');
 if(spec.retirement)return runRetiredChannel({id,root,spec,tool,repo:REPO,modulePath:MODULE,moduleInputs:MODULE_INPUTS});
 fs.mkdirSync(root,{recursive:true});const steps=[];let esbuild;
 try{
  const sources=[...new Set([...MODULE_INPUTS,...spec.sources])].sort(),snapshot=sources.map(rel=>{const p=sourceFile(REPO,rel),bytes=fs.readFileSync(p),copy=path.join(root,'input-sources',rel);fs.mkdirSync(path.dirname(copy),{recursive:true});fs.writeFileSync(copy,bytes);return {path:rel,sha256:sha(bytes),copy};});
  const moduleMembers=fs.readdirSync(path.join(REPO,MODULE)).filter(x=>/\.(?:mjs|cjs|json)$/.test(x)).sort();write(path.join(root,'input-snapshot.json'),{sources:snapshot,toolInputs:tool.files,moduleMembers});
  const require=createRequire(path.join(REPO,'package.json'));esbuild=require('esbuild');need(esbuild.version===tool.esbuildVersion,'PLUGIN_UPDATE_COMPILER_VERSION');
  const oldBinary=process.env.ESBUILD_BINARY_PATH;process.env.ESBUILD_BINARY_PATH=tool.esbuildExecutable;
  try{const actual=new Set(['package.json',spec.script,...(spec.manualReadInputs??[])]);for(const entry of spec.entries){const b=await esbuild.build({absWorkingDir:REPO,entryPoints:[entry],bundle:true,write:false,platform:'node',format:'esm',...(spec.target?{target:spec.target}:{}),metafile:true});for(const p of actualImports(b.metafile,REPO,REPO))actual.add(p);}need(JSON.stringify([...actual].sort())===JSON.stringify([...spec.sources].sort()),'PLUGIN_UPDATE_IMPORT_SET_CHANGED');}
  finally{esbuild.stop();if(oldBinary===undefined)delete process.env.ESBUILD_BINARY_PATH;else process.env.ESBUILD_BINARY_PATH=oldBinary;}
  function execute(label,sourceRoot,selectedFault){
   const negative=Boolean(selectedFault),dir=path.join(root,label),trace=path.join(dir,'bundles'),reports=path.join(dir,'reports'),failureFile=path.join(dir,'assertion-failure.json');fs.mkdirSync(dir,{recursive:true});
   const env={...process.env,ESBUILD_BINARY_PATH:tool.esbuildExecutable,YMCC_PLUGIN_TRACE_ROOT:trace,YMCC_PLUGIN_SOURCE_ROOT:sourceRoot};delete env.NODE_OPTIONS;delete env.NODE_PATH;delete env.YMCC_PLUGIN_FAILURE_FILE;delete env.YMCC_PLUGIN_ASSERTION_SCRIPT;
   if(spec.format==='ymcc-only-original-string-cases'){env.YMCC_PLUGIN_FAILURE_FILE=failureFile;env.YMCC_PLUGIN_ASSERTION_SCRIPT=path.join(sourceRoot,spec.script);}
   const command=[tool.nodeExecutable,'--require',path.join(REPO,MODULE,'observe-build.cjs'),path.join(sourceRoot,spec.script),reports],startedAt=new Date().toISOString();
   const p=spawnSync(command[0],command.slice(1),{cwd:sourceRoot,env,windowsHide:true,encoding:'utf8',timeout:30000,maxBuffer:2*1024*1024});
   fs.writeFileSync(path.join(dir,'stdout.log'),p.stdout??'');fs.writeFileSync(path.join(dir,'stderr.log'),p.stderr??'');steps.push({label,command,startedAt,exitCode:p.status,signal:p.signal,error:p.error?.code??null,pid:p.pid,naturalExit:!p.error&&!p.signal&&Number.isInteger(p.status)});need(!p.error&&!p.signal&&Number.isInteger(p.status),'PLUGIN_UPDATE_PRODUCER_TERMINATION');
   let verification,proofFile=path.join(reports,spec.report);
   if(negative&&spec.format==='ymcc-only-original-string-cases'){
    need(p.status===1&&!fs.existsSync(proofFile),'PLUGIN_UPDATE_YMCC_FAULT_MUST_EXIT_ON_ORIGINAL_ASSERTION');
    proofFile=failureFile;verification=verifyYmccAssertionFailure(read(failureFile),spec,selectedFault,path.join(sourceRoot,spec.script));
   }else{verification=verifyOriginal(read(proofFile),negative?{...spec,fault:selectedFault}:spec,{negative});need(negative?p.status!==0:p.status===0,'PLUGIN_UPDATE_PRODUCER_EXIT_MISMATCH');}
   const traces=fs.readdirSync(trace).filter(x=>/^\d+\.json$/.test(x)).sort();need(traces.length===spec.entries.length,'PLUGIN_UPDATE_BUILD_TRACE_MISSING');const actual=new Set(['package.json',spec.script,...(spec.manualReadInputs??[])]);
   for(const file of traces){const r=read(path.join(trace,file));need(r.esbuildVersion===tool.esbuildVersion&&r.outputFiles===1,'PLUGIN_UPDATE_COMPILER_TRACE_INVALID');for(const name of actualImports(r.metafile,r.workingDirectory,sourceRoot))actual.add(name);}
   need(JSON.stringify([...actual].sort())===JSON.stringify([...spec.sources].sort()),'PLUGIN_UPDATE_EXECUTED_IMPORT_SET_CHANGED');
   return {...verification,originalReport:proofFile,originalReportSha256:hash(proofFile),builds:traces.length,actualImportedSources:[...actual].sort(),negative};
  }
  const positive=execute('positive',REPO),declaredFaults=spec.faults??[spec.fault],detections=[];
  need(declaredFaults.length>0&&declaredFaults.length<=16,'PLUGIN_UPDATE_EXPLICIT_FAULTS_REQUIRED');
  for(const selectedFault of declaredFaults){
   const label=declaredFaults.length===1?'fault':'fault-'+selectedFault.id,mirror=path.join(root,label,'repo');
   for(const rel of spec.sources){const from=path.join(root,'input-sources',rel),to=path.join(mirror,rel);fs.mkdirSync(path.dirname(to),{recursive:true});fs.copyFileSync(from,to);}
   const dependency=path.join(mirror,'node_modules','esbuild');fs.mkdirSync(path.dirname(dependency),{recursive:true});fs.symlinkSync(tool.esbuildPackageRoot,dependency,process.platform==='win32'?'junction':'dir');
   const changed=path.join(mirror,selectedFault.path),before=fs.readFileSync(changed,'utf8');need(before.split(selectedFault.before).length===2,'PLUGIN_UPDATE_FAULT_ANCHOR_CHANGED');fs.writeFileSync(changed,before.replace(selectedFault.before,selectedFault.after));const mutatedHash=hash(changed);
   const changedSources=spec.sources.filter(rel=>hash(path.join(mirror,rel))!==hash(path.join(root,'input-sources',rel)));need(changedSources.length===1&&changedSources[0]===selectedFault.path,'PLUGIN_UPDATE_FAULT_NOT_SINGLE_SOURCE');
   const detected=execute(label,mirror,selectedFault);need(hash(path.join(mirror,spec.script))===hash(path.join(root,'input-sources',spec.script)),'PLUGIN_UPDATE_ORIGINAL_ASSERTIONS_CHANGED');
   for(const relative of spec.sources)need(hash(path.join(mirror,relative))===(relative===selectedFault.path?mutatedHash:hash(path.join(root,'input-sources',relative))),'PLUGIN_UPDATE_PRIVATE_FAULT_SOURCE_CHANGED');
   const proof={id:selectedFault.id??'original-semantic-fault',...detected,source:selectedFault.path,sourceFilesChanged:1,expectedFailure:selectedFault.expectedFailedCase};detections.push(proof);
   write(path.join(root,label,'mutation.json'),{source:selectedFault.path,before:selectedFault.before,after:selectedFault.after,originalSha256:sha(before),faultSha256:mutatedHash,originalAssertionScriptUnchanged:true,expectedFailure:selectedFault.expectedFailedCase,actualFailedNames:detected.failedNames,baselineIsSyntheticCurrentSourceFault:true,notHistoricalBefore:true});
  }
  const fault=detections[0];
  const drift=snapshot.filter(r=>hash(sourceFile(REPO,r.path))!==r.sha256||hash(r.copy)!==r.sha256);need(drift.length===0,'PLUGIN_UPDATE_SOURCE_CHANGED_DURING_RUN');for(const r of tool.files)need(hash(r.path)===r.sha256,'PLUGIN_UPDATE_TOOLCHAIN_CHANGED_DURING_RUN');need(JSON.stringify(fs.readdirSync(path.join(REPO,MODULE)).filter(x=>/\.(?:mjs|cjs|json)$/.test(x)).sort())===JSON.stringify(moduleMembers),'PLUGIN_UPDATE_MODULE_MEMBERSHIP_CHANGED');
  write(path.join(root,'process-ledger.json'),{steps});write(path.join(root,'actual-fault.json'),declaredFaults.length===1?read(path.join(root,'fault','mutation.json')):{faults:detections,sourceFilesChangedPerFault:1,originalAssertionScriptUnchanged:true});
  const outputs=walk(root),sourceHashes=[...snapshot.map(r=>({path:r.path,sha256:r.sha256})),...outputs.map(p=>({path:p,sha256:hash(p)}))];const result={schemaVersion:1,status:'PASS',passed:true,kind:'plugin-update-production-consumer',id,checks:positive.checks,positive,fault,negativeDetections:detections,sourceHashes,originalScriptsUnchanged:true,compilerObservation:'metafile and exact result.outputFiles bytes; original business assertions unchanged',process:{observed:steps.length,naturalExit:steps.every(x=>x.naturalExit),nestedEsbuildServicePidsObserved:false},boundary:{transportAndInstallActions:'inert original fixture callbacks',actualInstallations:0,networkRequests:0,steamUIActions:0,userStorageRead:false,physicalHardwareValidated:false,behaviorScope:spec.scope??'Original production updater service consumer',retiredNativeContractCovered:false},fullGoalComplete:false};write(path.join(root,'result.json'),result);return result;
 }catch(e){write(path.join(root,'failure.json'),{status:'FAIL',code:e.message,steps,originalFailurePreserved:true});throw e;}
 finally{esbuild?.stop();}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){try{const {id,root}=parseArgs(process.argv.slice(2)),r=await runCase(id,root);console.log(JSON.stringify({id,status:r.status,checks:r.checks,negativeDetected:r.negativeDetections?.map(x=>({id:x.id,failedNames:x.failedNames}))??r.fault?.failedNames??null,reason:r.reason??null,replacement:r.replacementEvidence??null,sourceHashes:r.sourceHashes.length}));if(r.status==='BLOCKED')process.exitCode=10;}catch(e){console.error(e.stack??e.message);process.exitCode=2;}}
