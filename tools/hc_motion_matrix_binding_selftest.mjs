// Offline actual-consumer regression and logical lookup-cost proof. No Host/device IO.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import {spawn} from 'node:child_process';
const self=fileURLToPath(import.meta.url),sha=bytes=>createHash('sha256').update(bytes).digest('hex'),digest=file=>sha(fs.readFileSync(file));
const atomic=(file,value)=>{const temp=file+'.tmp';fs.writeFileSync(temp,JSON.stringify(value,null,2)+'\n');fs.renameSync(temp,file);};
/** Stream evidence while the real owned child is alive, even if our caller kills
 * the checker. A RUNNING receipt never implies a completed compile/native run. */
export function runMatrixChild({out,label,exe,argv,cwd=out,timeoutMs=60000,maxBuffer=8*1024*1024}){
 if(!Number.isInteger(timeoutMs)||timeoutMs<1||timeoutMs>60000||maxBuffer!==8*1024*1024||!/^[A-Za-z-]+$/.test(label))throw Error('MATRIX_CHILD_LIMIT_OR_LABEL_INVALID');
 const startedAt=Date.now(),processFile=path.join(out,label+'.process.json'),log=path.join(out,label+'.log');
 const handles={stdout:fs.openSync(path.join(out,label+'.stdout.log'),'wx'),stderr:fs.openSync(path.join(out,label+'.stderr.log'),'wx'),combined:fs.openSync(log,'wx')};
 const input=exe==='cmd.exe'?argv[2]:exe;
 const receipt={schemaVersion:1,kind:'matrix-actual-child',state:'PREPARED',label,parentPid:process.pid,pid:null,executable:exe,args:argv,cwd,timeoutMs,maxBuffer,startedAt,phaseEvents:[],input:{path:input,beforeSha256:fs.existsSync(input)?digest(input):null},exitCode:null,signal:null,timedOut:false,launchError:null,outputTruncated:false};
 atomic(processFile,receipt);
 return new Promise(resolve=>{
  let child,stdout='',stderr='',bytes=0,partial='',timer;
  const append=(stream,chunk)=>{
   const buffer=Buffer.isBuffer(chunk)?chunk:Buffer.from(chunk),remaining=Math.max(0,maxBuffer-bytes);bytes+=buffer.length;
   if(buffer.length>remaining)receipt.outputTruncated=true;const kept=buffer.subarray(0,remaining);fs.writeSync(handles[stream],kept);fs.writeSync(handles.combined,kept);
   if(stream==='stdout'){stdout+=kept.toString('utf8');partial+=kept.toString('utf8');const lines=partial.split(/\r?\n/);partial=lines.pop();for(const line of lines){const m=/^__YMCC_MATRIX_PHASE__:(vcvars-start|vcvars-complete|compile-start|compile-complete)$/.exec(line.trim());if(m){receipt.phaseEvents.push({phase:m[1],elapsedMs:Date.now()-startedAt});atomic(processFile,receipt);}}}else stderr+=kept.toString('utf8');
  };
  const finish=(exitCode,signal)=>{
   clearTimeout(timer);for(const handle of Object.values(handles))fs.closeSync(handle);
   receipt.state='TERMINAL';receipt.exitCode=exitCode;receipt.signal=signal??null;receipt.finishedAt=Date.now();receipt.durationMs=receipt.finishedAt-startedAt;
   try{receipt.input.afterSha256=fs.existsSync(input)?digest(input):null;}catch(error){receipt.input.readError=error.code??'READ_FAILED';}
   receipt.streams=Object.fromEntries(['stdout','stderr'].map(stream=>[stream,{file:path.join(out,label+'.'+stream+'.log'),sha256:digest(path.join(out,label+'.'+stream+'.log'))}]));
   atomic(processFile,receipt);resolve({...receipt,stdout,stderr,status:exitCode,processFile,processSha256:digest(processFile)});
  };
  try{child=spawn(exe,argv,{cwd,windowsHide:true,shell:false,stdio:['ignore','pipe','pipe']});receipt.pid=child.pid??null;receipt.state='RUNNING';atomic(processFile,receipt);}
  catch(error){receipt.launchError=error.code??'SPAWN_FAILED';finish(null,null);return;}
  child.stdout.on('data',chunk=>append('stdout',chunk));child.stderr.on('data',chunk=>append('stderr',chunk));
  child.once('error',error=>{receipt.launchError=error.code??'SPAWN_FAILED';});child.once('close',finish);
  timer=setTimeout(()=>{receipt.timedOut=true;atomic(processFile,receipt);child.kill();},timeoutMs);
 });
}
const natural=r=>r.state==='TERMINAL'&&Number.isSafeInteger(r.pid)&&r.pid>0&&!r.signal&&!r.timedOut&&!r.launchError&&!r.outputTruncated;
export async function runMatrixChecker(args){
 const opt={};for(let i=0;i<args.length;i+=2){if(!['--out','--source','--repo','--generated','--fixture','--profiles','--expect-failure'].includes(args[i])||!args[i+1])throw Error('MATRIX_CHECKER_ARGUMENTS_INVALID');opt[args[i].slice(2)]=args[i+1];}
 const repo=path.resolve(opt.repo??path.join(path.dirname(self),'..')),out=path.resolve(opt.out??''),source=path.resolve(opt.source??repo+'/native/main.cpp'),fixture=path.resolve(opt.fixture??repo+'/tools/fixtures/hc_model_motion_consumer/matrix_binding_probe.cpp'),relative=path.relative(repo,out);
 if(!opt.out||relative===''||(!relative.startsWith('..'+path.sep)&&!path.isAbsolute(relative))||fs.existsSync(out))throw Error('MATRIX_FRESH_OUTSIDE_REPO_REQUIRED');fs.mkdirSync(out,{recursive:true});
 const result={schemaVersion:1,status:'RUNNING',expectedSemanticFailure:opt['expect-failure']??null,source,sourceSha256:null,inputs:[],configurations:[],drift:[],processes:[],fullBusExecuted:false,hardwareIo:false,physicalAcceptance:false,wholeProcessCpuReduction:null};
 const save=()=>atomic(out+'/result.json',result);
 const run=async(label,exe,argv,cwd=out)=>{const r=await runMatrixChild({out,label,exe,argv,cwd});result.processes.push({file:r.processFile,sha256:r.processSha256,label,pid:r.pid,exitCode:r.exitCode,signal:r.signal,timedOut:r.timedOut,durationMs:r.durationMs});save();return r;};
 const required=(r,phase)=>{if(!natural(r)||r.exitCode!==0)throw Object.assign(Error(r.timedOut?'MATRIX_CHILD_TIMEOUT':phase),{evidence:{processFile:r.processFile,pid:r.pid,exitCode:r.exitCode,signal:r.signal,timedOut:r.timedOut,launchError:r.launchError,outputTruncated:r.outputTruncated}});};
 save();
 try{
  const extractor=await import(pathToFileURL(path.join(repo,'tools/fixtures/legion_motion_consumer/extract_main.mjs'))),closing=await import(pathToFileURL(path.join(repo,'tools/fixtures/hc_remaining_model_capabilities/expectations.mjs'))),toolchain=await import(pathToFileURL(path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs')));
  const generated=opt.generated?path.resolve(opt.generated):out+'/generated';
  if(!opt.generated)required(await run('generate',process.execPath,[repo+'/tools/hc_model_motion_consumer_selftest.mjs','--source',source,'--out',generated,'--generate-only']),'MATRIX_GENERATOR_FAILED');
  const bound=JSON.parse(fs.readFileSync(generated+'/source-bindings.json','utf8'));if(bound.source.sha256!==digest(source)||bound.generatedCppSha256!==digest(generated+'/motion_consumer.cpp'))throw Error('MATRIX_SOURCE_GENERATED_DRIFT');
  result.sourceSha256=digest(source);result.sourceBindings={file:generated+'/source-bindings.json',sha256:digest(generated+'/source-bindings.json')};
  const inputs=[self,fixture,source,generated+'/source-bindings.json',generated+'/motion_consumer.cpp',generated+'/expectations.json',repo+'/tools/fixtures/legion_motion_consumer/extract_main.mjs',repo+'/tools/fixtures/hc_remaining_model_capabilities/expectations.mjs',repo+'/tools/fixtures/hc_reviewed_calibration/toolchain.mjs'];
  const inc=out+'/source';fs.mkdirSync(inc);for(const entry of fs.readdirSync(generated))if(entry.endsWith('.h')||entry==='unified_binding.cpp'){fs.copyFileSync(generated+'/'+entry,inc+'/'+entry);inputs.push(generated+'/'+entry);}
  let code=fs.readFileSync(generated+'/motion_consumer.cpp','utf8');
  const ns=code.indexOf('namespace candidate {'),end=closing.closeBrace(code,code.indexOf('{',ns));if(ns<0)throw Error('MATRIX_CANDIDATE_NAMESPACE');const body=code.slice(ns,end+1),matches=[...body.matchAll(/^static ProbeMotionResult probeActive[^\n]*\) \{/gm)];if(matches.length!==1)throw Error('MATRIX_ACTIVE_PREFIX_DEFINITION');const activeStart=matches[0].index,active=body.slice(activeStart,closing.closeBrace(body,activeStart+matches[0][0].lastIndexOf('{'))+1);
  let proof=fs.readFileSync(fixture,'utf8');const frozen=/Baseline function SHA256: ([a-f0-9]{64})/.exec(proof);if(!frozen||sha(extractor.extract(proof,'inputCaptureApplyHcMatrixCandidate').code.replaceAll('\r\n','\n'))!==frozen[1])throw Error('MATRIX_FROZEN_COMPARISON_CHANGED');if(proof.split('@@BASELINE_ACTIVE_PROBE@@').length!==2)throw Error('MATRIX_ACTIVE_PREFIX_MARKER');proof=proof.replace('@@BASELINE_ACTIVE_PROBE@@',active);
  // Only this uncalled harness entry is omitted. All production definitions,
  // immutable baseline, matrix/BUS probes and the entire new main remain intact.
  const entry='int main(int argc,char** argv)',start=code.indexOf(entry);if(start<0||code.indexOf(entry,start+1)>=0||(code.match(/\bmain\s*\(/g)??[]).length!==1)throw Error('MATRIX_SINGLE_UNCALLED_HARNESS_MAIN_REQUIRED');
  const stop=closing.closeBrace(code,code.indexOf('{',start))+1,omitted=code.slice(start,stop),prefix=code.slice(0,start),suffix=code.slice(stop);
  const omittedEntry={symbol:'main',startUtf8Byte:Buffer.byteLength(prefix),endUtf8ByteExclusive:Buffer.byteLength(code.slice(0,stop)),sha256:sha(omitted),bytes:Buffer.byteLength(omitted),keptPrefixSha256:sha(prefix),keptSuffixSha256:sha(suffix),generatedConsumerSha256:sha(code),references:1};
  code=prefix+'/* Matrix TU omits the uncalled full-suite entry only. */'+suffix+'\n'+proof;
  const headerFile=inc+'/hc_model_motion_catalog.snapshot.h',headerBefore=fs.readFileSync(headerFile,'utf8'),findStart=headerBefore.indexOf('inline constexpr const Recipe* find('),findEnd=closing.closeBrace(headerBefore,headerBefore.indexOf('{',findStart)),find=headerBefore.slice(findStart,findEnd+1);if(findStart<0||headerBefore.indexOf('inline constexpr const Recipe* find(',findStart+1)>=0)throw Error('MATRIX_EXACT_FIND_DEFINITION');
  const comparison='recipe.deviceClass == deviceClass';if(find.split(comparison).length!==2)throw Error('MATRIX_LOOKUP_COMPARISON_MOVED');
  const open=find.indexOf('{'),counterFind=(find.slice(0,open+1)+'\n    if(!std::is_constant_evaluated())++matrixProbeFindCalls;'+find.slice(open+1)).replace(comparison,'matrixProbeObservedEqual(recipe.deviceClass,deviceClass)');
  const observer='inline unsigned long long matrixProbeFindCalls=0,matrixProbeComparisons=0;\ninline constexpr bool matrixProbeObservedEqual(std::string_view left,std::string_view right) noexcept {if(!std::is_constant_evaluated())++matrixProbeComparisons;return left==right;}\n';
  const header=headerBefore.replace('#include <string_view>','#include <string_view>\n#include <type_traits>').replace(find,observer+counterFind);if(header===headerBefore)throw Error('MATRIX_LOOKUP_OBSERVER_SEAM');fs.writeFileSync(headerFile,header);fs.writeFileSync(inc+'/probe.cpp',code);
  result.inputs=[...new Set(inputs)].map(file=>({file,sha256:digest(file)}));result.instrumentation={productionMatrixAndBusBodiesUnmodified:true,lookupHeaderOriginalSha256:sha(headerBefore),lookupHeaderInstrumentedSha256:sha(header),logicalEntryAndComparisonCountersOnly:true,originalLookupAlgorithmPreserved:true,immutableOriginalComparisonSha256:frozen[1],unusedFullSuiteEntryOmitted:true,omittedEntry};save();
  for(const profile of (opt.profiles??'Release,Checked').split(',')){
   if(!['Release','Checked'].includes(profile))throw Error('MATRIX_PROFILE_INVALID');const dir=out+'/'+profile;fs.mkdirSync(dir);
   const flags=profile==='Release'?'/O2 /DNDEBUG /MD':'/Od /RTC1 /Zi /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2 /MDd',batch=dir+'/compile.cmd';
   const preamble=toolchain.calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat').replace('@echo off\r\n','@echo off\r\necho __YMCC_MATRIX_PHASE__:vcvars-start\r\n').replace('if errorlevel 1 exit /b 1\r\n','if errorlevel 1 exit /b 1\r\necho __YMCC_MATRIX_PHASE__:vcvars-complete\r\n');
   fs.writeFileSync(batch,preamble+'chcp 65001 >nul\r\necho __YMCC_MATRIX_PHASE__:compile-start\r\ncl /nologo /std:c++20 /EHsc /utf-8 /W4 /permissive- '+flags+' /I"'+repo+'/deps/json" "'+inc+'/probe.cpp" /Fo"'+dir+'/probe.obj" /Fd"'+dir+'/probe.pdb" /Fe"'+dir+'/probe.exe" /sourceDependencies "'+dir+'/dependencies.json" /link /INCREMENTAL:NO\r\nset "YMCC_MATRIX_CL_EXIT=%errorlevel%"\r\necho __YMCC_MATRIX_PHASE__:compile-complete\r\nexit /b %YMCC_MATRIX_CL_EXIT%\r\n');
   const build=await run(profile+'-compile','cmd.exe',['/d','/c',batch],dir);required(build,'MATRIX_COMPILE_FAILED');
   const execution=await run(profile+'-run',dir+'/probe.exe',[generated+'/expectations.json'],dir);if(!natural(execution))throw Object.assign(Error('MATRIX_NATIVE_PROCESS_FAILED'),{evidence:{processFile:execution.processFile,pid:execution.pid,exitCode:execution.exitCode,signal:execution.signal,timedOut:execution.timedOut}});
   let native;try{native=JSON.parse(execution.stdout.trim());}catch{throw Error('MATRIX_NATIVE_REPORT_MISSING');}
   const expected=opt['expect-failure'],accepted=expected?execution.exitCode===1&&native.status==='FAIL'&&native.error===expected:execution.exitCode===0&&native.status==='PASS';
   const dependency=JSON.parse(fs.readFileSync(dir+'/dependencies.json','utf8').replace(/^\uFEFF/,''));
   result.configurations.push({profile,buildExit:build.exitCode,executionExit:execution.exitCode,accepted,result:native,executableSha256:digest(dir+'/probe.exe'),compileProcess:{file:build.processFile,sha256:build.processSha256},nativeProcess:{file:execution.processFile,sha256:execution.processSha256},actualIncludes:[dependency.Data.Source,...dependency.Data.Includes].map(file=>({file,sha256:digest(file)}))});save();
   console.log(JSON.stringify({profile,accepted,status:native.status,checks:native.checks,transforms:native.matrixTransformPairs,oldFind:native.oldMatrixReviewedFindCalls,newFind:native.newMatrixReviewedFindCalls,error:native.error}));
  }
  result.drift=result.inputs.filter(row=>digest(row.file)!==row.sha256);result.status=result.configurations.every(c=>c.accepted)&&!result.drift.length?'PASS':'FAIL';if(result.status!=='PASS')result.failure={code:'MATRIX_NATIVE_ASSERTION_OR_SOURCE_DRIFT'};
 }catch(error){result.status='FAIL';result.failure={code:/^[A-Z_]+$/.test(error.message)?error.message:'MATRIX_CHECKER_ERROR',detail:String(error.message).slice(0,400),...(error.evidence?{evidence:error.evidence}:{})};}
 save();return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===self){try{const result=await runMatrixChecker(process.argv.slice(2));process.exitCode=result.status==='PASS'?0:1;}catch(error){console.error(error.stack);process.exitCode=2;}}
