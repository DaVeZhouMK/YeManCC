import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {spawn} from 'node:child_process';
import {finished} from 'node:stream/promises';
import {PRODUCERS,OVERLAY_FILES,FAULTS,need,same,requireNatural,validateOriginal,validateFaultSummary} from './contracts.mjs';
import {BASELINE,HC_SOURCE_FILES} from './inputs.mjs';
export const key=file=>path.resolve(file).toLowerCase();
export const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
export const digest=file=>sha(fs.readFileSync(file));
export const readJson=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
export function inside(root,file){const rel=path.relative(root,file);return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));}
function noReparse(file){for(let cursor=path.resolve(file);;cursor=path.dirname(cursor)){if(fs.existsSync(cursor))need(!fs.lstatSync(cursor).isSymbolicLink(),'REPARSE_PATH');if(path.dirname(cursor)===cursor)break;}}
export function freshRoot(out,repo){
 need(typeof out==='string'&&path.isAbsolute(out)&&!/[\0\r\n]/.test(out),'ABSOLUTE_EVIDENCE_REQUIRED');
 out=path.resolve(out);need(!inside(fs.realpathSync(repo),out)&&out!==path.parse(out).root&&!out.startsWith('\\\\'),'EVIDENCE_LOCATION');
 noReparse(out);need(!fs.existsSync(out),'FRESH_EVIDENCE_REQUIRED');
 need(fs.statSync(path.dirname(out)).isDirectory(),'EXISTING_EVIDENCE_PARENT_REQUIRED');
 fs.mkdirSync(out);return out;
}
export function writeNew(file,value){fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});}
export function snapshot(files){return files.map(file=>{try{noReparse(file);const realPath=fs.realpathSync(file),stat=fs.statSync(file);need(stat.isFile(),'INPUT_NOT_FILE');return {path:file,realPath,bytes:stat.size,sha256:digest(file),status:'OK'};}catch(error){return {path:file,status:'MISSING_OR_INVALID',error:String(error.message)};}});}
export function pointer(root,file){need(inside(root,path.resolve(file)),'RAW_EVIDENCE_ESCAPE');noReparse(file);need(fs.statSync(file).isFile(),'RAW_EVIDENCE_NOT_FILE');return {path:path.resolve(file),relative:path.relative(root,file).replaceAll('\\','/'),bytes:fs.statSync(file).size,sha256:digest(file)};}
export function inventory(root,dir){
 if(!fs.existsSync(dir))return [];
 const rows=[];function visit(folder){noReparse(folder);for(const entry of fs.readdirSync(folder,{withFileTypes:true})){const file=path.join(folder,entry.name);need(!entry.isSymbolicLink(),'RAW_REPARSE');if(entry.isDirectory())visit(file);else{need(entry.isFile()&&rows.length<10000,'RAW_TREE_INVALID');rows.push(pointer(root,file));}}}visit(dir);return rows.sort((a,b)=>a.relative.localeCompare(b.relative));
}
export function validateSourceLocks(repo,sourceRows){
 need(sourceRows.every(row=>row.status==='OK'&&inside(fs.realpathSync(repo),row.realPath)),'SOURCE_MISSING_OR_ESCAPED');
 const known=new Map(sourceRows.map(row=>[key(row.path),row.sha256]));
 for(const p of PRODUCERS)need(known.get(key(path.join(repo,p.path)))===p.sha256,'ORIGINAL_PRODUCER_BYTES_CHANGED');
 const baseline=readJson(path.join(repo,BASELINE,'BASELINE.json'));
 need(baseline.schemaVersion===1&&baseline.files?.length===2,'BASELINE_INDEX');
 for(const row of baseline.files)need(known.get(key(path.join(repo,BASELINE,row.file)))===row.sha256,'FROZEN_SOURCE_CHANGED');
 const nativeOriginal=baseline.originalFiles?.find(row=>row.file==='native/msi_native_rear_keyboard_runtime.h');
 need(nativeOriginal&&known.get(key(path.join(repo,nativeOriginal.file)))===nativeOriginal.sha256,'NATIVE_RUNTIME_MUST_REMAIN_ORIGINAL');
 const policy=readJson(path.join(repo,'capabilities/hc-controller-configuration-policy.json'));
 need(same(policy.sourceFiles?.map(row=>row.file),HC_SOURCE_FILES)&&policy.physicalAcceptance===false&&policy.automaticRestoreEstablished===false&&policy.sourceFreshnessOwnedByConsumer===true,'POLICY_BOUNDARY');
 need(policy.routes?.length===2&&policy.routes[0].name==='DInputButtons'&&policy.routes[0].productEnabled===true&&policy.routes[0].currentConsumerWired===true&&policy.routes[1].name==='XInputKeyboard'&&policy.routes[1].productEnabled===false&&policy.routes[1].currentConsumerWired===false,'POLICY_ENABLED_SD_ONLY');
 const locked=new Map(fs.readFileSync(path.join(repo,'deps/handheldcompanion-runtime/source-files.sha256'),'utf8').trim().split(/\r?\n/).map(line=>{const m=/^([a-f0-9]{64})\s+(.+)$/i.exec(line);need(m,'HC_LOCK_INDEX');return [m[2],m[1].toLowerCase()];}));
 for(const row of policy.sourceFiles)need(locked.get(row.file)===row.sha256&&known.get(key(path.join(repo,'deps/handheldcompanion-runtime/source',row.file)))===row.sha256,'HC_LOCKED_SOURCE_CHANGED');
 need(policy.provider?.file==='native/msi_rear_protocol.h'&&known.get(key(path.join(repo,policy.provider.file)))===policy.provider.sha256,'PROVIDER_CHANGED');
 return known;
}
export function producerEnvironment(spec){
 const env={...process.env};
 const erased=['NODE_OPTIONS','NODE_PATH','CL','_CL_','LINK','_LINK_','INCLUDE','LIB','LIBPATH','VSCMD_VER','VSCMD_ARG_TGT_ARCH','VSCMD_ARG_HOST_ARCH','VSCMD_SKIP_SENDTELEMETRY','__VSCMD_PREINIT_PATH'];
 for(const name of Object.keys(env))if(erased.some(k=>k.toLowerCase()===name.toLowerCase()))delete env[name];
 for(const name of Object.keys(env))if(['path','comspec','psmodulepath'].includes(name.toLowerCase()))delete env[name];
 env.PATH=[path.dirname(spec.tools.cmdExecutable),process.env.SystemRoot??'C:/Windows',path.dirname(spec.tools.nodeExecutable),path.dirname(spec.powershellExecutable)].join(path.delimiter);
 env.ComSpec=spec.tools.cmdExecutable;env.PSModulePath=path.join(path.dirname(spec.powershellExecutable),'Modules');
 return env;
}
/** Streams go straight to new files without a bounded string capture or truncation. */
export async function runProducer({root,label,exe,args,cwd,env,deadline}){
 const startedAt=new Date().toISOString(),started=Date.now();
 const stdoutFile=path.join(root,label+'.stdout.log'),stderrFile=path.join(root,label+'.stderr.log');
 const stdout=fs.createWriteStream(stdoutFile,{flags:'wx'}),stderr=fs.createWriteStream(stderrFile,{flags:'wx'});
 let timedOut=false,launchError=null,stdoutComplete=false,stderrComplete=false;
 const child=spawn(exe,args,{cwd,env,windowsHide:true,stdio:['ignore','pipe','pipe']});
 child.stdout.on('end',()=>{stdoutComplete=true;});child.stderr.on('end',()=>{stderrComplete=true;});
 child.stdout.pipe(stdout);child.stderr.pipe(stderr);
 const fail=error=>{launchError=String(error.code??error.message);child.kill();};
 stdout.on('error',fail);stderr.on('error',fail);child.on('error',error=>{launchError=String(error.code??error.message);});
 const timer=setTimeout(()=>{timedOut=true;child.kill();child.stdout.destroy();child.stderr.destroy();stdout.end();stderr.end();},Math.max(1,deadline-Date.now()));
 const terminal=await new Promise(resolve=>child.once('close',(exitCode,signal)=>resolve({exitCode,signal})));
 clearTimeout(timer);
 const drains=await Promise.allSettled([finished(stdout),finished(stderr)]);
 if(drains.some(row=>row.status==='rejected'))launchError??='RAW_OUTPUT_WRITE_FAILED';
 const result={label,pid:child.pid??null,exe,args,cwd,startedAt,finishedAt:new Date().toISOString(),durationMs:Date.now()-started,...terminal,timedOut,launchError,outputTruncated:false,stdoutComplete,stderrComplete,stdout:pointer(root,stdoutFile),stderr:pointer(root,stderrFile)};
 writeNew(path.join(root,label+'-terminal.json'),result);return result;
}
function expectInputs(repo,sourceRoot){return [...new Set([
 path.join(repo,PRODUCERS[0].path),path.join(sourceRoot,'tools/fixtures/hc_msi_rear_consumers/probe.cpp'),path.join(repo,BASELINE,'BASELINE.json'),
 path.join(repo,BASELINE,'native/main.cpp'),path.join(repo,BASELINE,'native/msi_native_rear_keyboard_runtime.h'),
 ...OVERLAY_FILES.filter(f=>f!=='tools/fixtures/hc_msi_rear_consumers/probe.cpp').map(f=>path.join(sourceRoot,f)),
])];}
function validateFragments(file,expectedInputs){
 const raw=readJson(file);need(raw.bodiesRewritten===false&&raw.externalFunctionsAreMemoryFakes===true&&raw.fragments?.length===15,'ORIGINAL_FRAGMENTS');
 const allowed=new Set(expectedInputs.map(key)),seen=new Set();
 for(const row of raw.fragments){
  need(allowed.has(key(row.file))&&!seen.has(key(row.file)+'#'+row.symbol),'FRAGMENT_SOURCE');seen.add(key(row.file)+'#'+row.symbol);
  const bytes=fs.readFileSync(row.file);need(Number.isSafeInteger(row.startUtf8Byte)&&Number.isSafeInteger(row.endUtf8ByteExclusive)&&row.startUtf8Byte>=0&&row.endUtf8ByteExclusive>row.startUtf8Byte&&row.endUtf8ByteExclusive<=bytes.length,'FRAGMENT_RANGE');
  need(bytes.subarray(row.startUtf8Byte,row.endUtf8ByteExclusive).toString('utf8')===row.code&&sha(row.code)===row.sha256,'FRAGMENT_BYTES_CHANGED');
 }
}
export function validateExecution({root,dir,repo,sourceRoot,sourceRows,toolRows,fault=null}){
 const resultFile=path.join(dir,'result.json'),raw=readJson(resultFile),c=validateOriginal(raw,fault);
 const expected=expectInputs(repo,sourceRoot),declared=new Map([...sourceRows,...toolRows].map(row=>[key(row.path),row.sha256]));
 need(raw.inputs?.length===expected.length&&new Set(raw.inputs.map(row=>key(row.file))).size===expected.length&&expected.every(file=>raw.inputs.some(row=>key(row.file)===key(file))),'ORIGINAL_FULL_INPUTS');
 if(fault){for(const name of OVERLAY_FILES){const file=path.join(sourceRoot,name),original=declared.get(key(path.join(repo,name))),hash=digest(file);need(name===fault.source?hash!==original:hash===original,'OVERLAY_OUTSIDE_ONE_FAULT');declared.set(key(file),hash);}}
 for(const row of raw.inputs)need(declared.get(key(row.file))===row.sha256&&digest(row.file)===row.sha256,'ORIGINAL_INPUT_IDENTITY');
 const fragments=path.join(dir,'source-fragments.json');validateFragments(fragments,expected);
 const compileCmd=path.join(dir,'DefaultRelease/compile.cmd'),command=fs.readFileSync(compileCmd,'utf8');
 need(!/\/D\s*YMCC_MSI_NATIVE_REAR_KEYBOARD_EXPERIMENT/i.test(command)&&command.includes('/DYMCC_MSI_BLOCKED_START_PREPARATION_FIX'),'EXPERIMENT_COMPILE_FORBIDDEN');
 need(fs.readdirSync(dir,{withFileTypes:true}).filter(x=>x.isDirectory()).every(x=>x.name==='DefaultRelease'),'UNEXPECTED_CONFIGURATION_OUTPUT');
 const compilerDependencies=path.join(dir,'DefaultRelease/dependencies.json'),deps=readJson(compilerDependencies).Data;
 need(typeof deps?.Source==='string'&&Array.isArray(deps.Includes),'RAW_SOURCE_DEPENDENCIES_REQUIRED');
 const actual=[deps.Source,...deps.Includes],generated=path.join(dir,'probe.cpp');declared.set(key(generated),digest(generated));
 need(key(deps.Source)===key(generated)&&Array.isArray(c.dependencies)&&actual.length===c.dependencies.length&&actual.length>1,'SOURCE_DEPENDENCIES_NOT_COMPLETE');
 const observedIncludes=actual.map((file,i)=>{const row=c.dependencies[i],hash=digest(file);need(key(row.file)===key(file)&&row.sha256===hash&&declared.get(key(file))===hash,'UNDECLARED_OR_CHANGED_INCLUDE');return {path:file,sha256:hash,role:key(file)===key(generated)?'this-run-generated-source':inside(root,path.resolve(file))?'this-run-source-overlay':sourceRows.some(r=>key(r.path)===key(file))?'declared-source':'predeclared-tool-observed-include'};});
 const stdoutFile=path.join(dir,'DefaultRelease-run.stdout.log'),stderrFile=path.join(dir,'DefaultRelease-run.stderr.log');
 need(same(readJson(stdoutFile),c.report),'RAW_NATIVE_REPORT_MISMATCH');
 const files=['DefaultRelease-compile.stdout.log','DefaultRelease-compile.stderr.log','DefaultRelease-run.stdout.log','DefaultRelease-run.stderr.log','DefaultRelease/compile.cmd','DefaultRelease/dependencies.json','DefaultRelease/probe.exe','DefaultRelease/probe.obj','probe.cpp','source-fragments.json','result.json'];
 const rawFiles=files.map(f=>pointer(root,path.join(dir,f)));
 return {validated:true,result:pointer(root,resultFile),configuration:c.configuration,compiled:true,nativeExitCode:c.exitCode,nativeSignal:c.signal,report:c.report,observedIncludes,rawFiles,
  nativeProcessEvidence:'pinned original producer checked compile status, native exit/signal and complete buffered output before writing this result; no independent native PID trace'};
}
export function validatePositiveProducerOutput(call,positive){requireNatural(call);need(same(readJson(call.stdout.path),{configuration:'DefaultRelease',...positive.report}),'POSITIVE_PRODUCER_STDOUT');}
export function validateFaultExecutions({root,repo,sourceRows,toolRows,call}){
 requireNatural(call);const dir=path.join(root,'faults'),file=path.join(dir,'result.json'),raw=readJson(file),rows=validateFaultSummary(raw);
 need(same(fs.readdirSync(dir,{withFileTypes:true}).filter(x=>x.isDirectory()).map(x=>x.name).sort(),FAULTS.map(x=>x.id).sort()),'EXPERIMENT_OR_EXTRA_FAULT_DIRECTORY');
 const lines=fs.readFileSync(call.stdout.path,'utf8').trim().split(/\r?\n/).map(line=>JSON.parse(line));need(same(lines,rows),'FAULT_PRODUCER_STDOUT');
 const faults=FAULTS.map((fault,i)=>{
  const execution=path.join(dir,fault.id,'execution'),row=rows[i],child=validateExecution({root,dir:execution,repo,sourceRoot:path.join(dir,fault.id,'source'),sourceRows,toolRows,fault});
  need(key(row.evidence)===key(child.result.path)&&row.evidenceSha256===child.result.sha256,'FAULT_RESULT_POINTER_OR_HASH');
  const stdout=path.join(dir,fault.id,'runner.stdout.log'),stderr=path.join(dir,fault.id,'runner.stderr.log');
  need(same(readJson(stdout),{configuration:'DefaultRelease',...child.report}),'FAULT_CHILD_STDOUT');
  return {id:fault.id,...child,expectedError:fault.error,semanticRejected:true,childExitCode:0,childSignal:null,
   childExitEvidence:'pinned original fault producer requires child.status===0, child.signal===null and rejected=true; raw runner streams preserved',
   childStdout:pointer(root,stdout),childStderr:pointer(root,stderr)};
 });
 return {faults,faultSummary:{validated:true,result:pointer(root,file),faultIds:rows.map(row=>row.id)}};
}
