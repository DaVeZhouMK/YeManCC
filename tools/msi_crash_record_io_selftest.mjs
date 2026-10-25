import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {isDeepStrictEqual} from 'node:util';
import {extractFunction} from './ymcc_sim/global/controller-startup-return/extend-fixture.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const HERE=path.dirname(fileURLToPath(import.meta.url)), DEFAULT_REPO=path.resolve(HERE,'..');
const MODULE='tools/ymcc_sim/global/controller-startup-return';
const FUNCTIONS=['sgWriteFileAtomicFlags','sgWriteFileAtomic','msiCrashReadOwnedRecord'];
const INPUTS=['native/main.cpp','native/msi_crash_recovery_runtime.h','native/msi_crash_recovery_journal.h','deps/json/json.hpp',
 'tools/msi_crash_record_io_selftest.cpp','tools/msi_crash_record_io_selftest.mjs',MODULE+'/extend-fixture.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'];
const CASES=[['journal/new-durable',6],['journal/replace-old',4],['journal/real-sharing-and-release',6],
 ['leaf/write-failure',6],['leaf/short-write',6],['leaf/flush-failure',6],['leaf/rename-failure',6],['leaf/replace-fallback',3],
 ['reader/exact-64KiB',3],['reader/oversize',3],['reader/empty',3],['reader/missing-and-sharing',2],
 ['leaf/read-failure',2],['leaf/short-read',2],['leaf/changing-info',2],['writer/owned-child-before-rename',7]];
const failure=(name,assertions)=>assertions.map(assertion=>({case:name,assertion}));
const FAULTS={
 'ignore-flush-failure':{before:'if (durable && writeOk && !FlushFileBuffers(file)) writeOk = false;',
  after:'if (durable && writeOk && !FlushFileBuffers(file)) writeOk = true;',failures:failure('leaf/flush-failure',[
   'failed IO returns false','failed IO retains exact old content and ownership','failed IO never publishes a new record'])},
 'ignore-write-failure':{before:'if (!WriteFile(file, content.data() + offset, chunk, &written, nullptr) || written != chunk) {\n            writeOk = false;',
  after:'if (!WriteFile(file, content.data() + offset, chunk, &written, nullptr) || written != chunk) {\n            writeOk = true;',
  failures:['leaf/write-failure','leaf/short-write'].flatMap(name=>failure(name,[
   'failed IO returns false','failed IO retains exact old content and ownership','failed IO never publishes a new record','failed IO preserves flush ordering']))}
};
const need=(v,m)=>{if(!v)throw Error('MSI_RECORD_IO_'+m);};
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const digest=s=>crypto.createHash('sha256').update(s).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const write=(p,d)=>fs.writeFileSync(p,JSON.stringify(d,null,2)+'\n',{flag:'wx'});
const inside=(root,p)=>{const r=path.relative(path.resolve(root),path.resolve(p));return r!==''&&r!=='..'&&!r.startsWith('..'+path.sep)&&!path.isAbsolute(r);};
function extract(source,name){
 // The shared extractor accepts static declarations. Add/remove only that token
 // for the non-static writer, retaining the exact original code, lines and hash.
 if(name!=='sgWriteFileAtomicFlags')return extractFunction(source,name);
 const match=/^bool sgWriteFileAtomicFlags\(/m.exec(source);need(match,'WRITER_SIGNATURE_MISSING');
 const r=extractFunction(source.slice(0,match.index)+'static '+source.slice(match.index),name);
 r.code=r.code.slice(7);r.sha256=digest(r.code);need(source.slice(match.index,match.index+r.code.length)===r.code,'NONSTATIC_EXTRACT_NOT_EXACT');return r;
}
function outputRoot(value,repo,toolchainFile){
 need(typeof value==='string'&&path.isAbsolute(value)&&!/[\0\r\n]/.test(value),'ABSOLUTE_OUTPUT_REQUIRED');
 let workspace=HERE;while(!fs.existsSync(path.join(workspace,'Mainline/YeManCC-source/YeManCC/AGENTS.md'))){const parent=path.dirname(workspace);need(parent!==workspace,'WORKSPACE_NOT_FOUND');workspace=parent;}
 const root=path.resolve(value),validation=path.join(workspace,'Build/Validation/VirtualController-Lifecycle-20261019');
 const caseRoot=path.dirname(root),marker=path.join(caseRoot,'input-snapshot.json');let publicCase=false;
 if(path.basename(root)==='i'&&path.basename(caseRoot)==='startup-return'&&path.basename(path.dirname(caseRoot))==='controller.startup-return'&&
    path.basename(path.dirname(path.dirname(caseRoot)))==='cases'&&inside(path.join(workspace,'Build'),root)&&fs.existsSync(marker)){
  const captured=read(marker);need(captured.schemaVersion===1&&Array.isArray(captured.sources),'PUBLIC_SNAPSHOT_SCHEMA');
  need(path.resolve(repo)===path.join(caseRoot,'frozen-project'),'PUBLIC_REPO_NOT_FROZEN_PROJECT');
  for(const rel of INPUTS){const row=captured.sources.find(r=>r.path===rel);need(row&&inside(caseRoot,row.copy)&&hash(row.copy)===row.sha256&&hash(path.join(repo,rel))===row.sha256,'PUBLIC_SOURCE_NOT_FROZEN:'+rel);}
  const tool=captured.sources.find(r=>r.path===MODULE+'/toolchain-inputs.json');
  need(tool&&path.resolve(toolchainFile)===path.resolve(tool.copy)&&hash(toolchainFile)===tool.sha256,'PUBLIC_TOOLCHAIN_NOT_FROZEN');publicCase=true;
 }
 need(inside(validation,root)||publicCase,'OUTPUT_OUTSIDE_PRIVATE_OR_FROZEN_PUBLIC_CASE');
 for(let p=root;;p=path.dirname(p)){if(fs.existsSync(p))need(!fs.lstatSync(p).isSymbolicLink(),'OUTPUT_REPARSE');if(p===path.dirname(p))break;}
 need(!fs.existsSync(root),'FRESH_OUTPUT_REQUIRED');return root;
}
export function verifyRecordIoProfile(raw,{variant='original',buildExitCode,executeExitCode}={}){
 need(buildExitCode===0,'COMPILE_FAILURE_IS_NOT_SEMANTIC_REJECTION');need(variant==='original'||Object.hasOwn(FAULTS,variant),'UNKNOWN_VARIANT');
 need(raw?.schemaVersion===1&&raw.tests===16&&raw.assertions===67&&Array.isArray(raw.cases),'MISSING_OR_INCOMPLETE_REPORT');
 need(isDeepStrictEqual(raw.cases.map(c=>[c.name,c.assertions]),CASES),'ORIGINAL_ASSERTIONS_CHANGED');
 const failures=variant==='original'?[]:FAULTS[variant].failures;need(isDeepStrictEqual(raw.failures,failures),'WRONG_SEMANTIC_FAILURES:'+variant+':'+JSON.stringify(raw.failures));
 need(raw.passed===(failures.length===0)&&executeExitCode===(failures.length?1:0),'EXIT_OR_PASS_CONTRADICTION');
 const failed=new Set(failures.map(f=>f.case));for(const c of raw.cases){
  need(c.passed===!failed.has(c.name),'CASE_PASS_CONTRADICTION');
  need(c.boundary===(c.name.startsWith('leaf/')?'explicit-error-leaf':'private-real-Win32'),'BOUNDARY_UNLABELLED');
  need(c.name.startsWith('leaf/')?c.explicitLeafCalls>0:c.explicitLeafCalls===0,'REAL_AND_LEAF_EVIDENCE_MIXED');
 }
 need(raw.productRun===false&&raw.hardwareAccess===false&&raw.filesystem==='private-real-Win32'&&raw.rebootOrPowerLossTested===false,'BOUNDARY_MISSING');
 need(Number.isInteger(raw.ownedChildPid)&&raw.ownedChildPid>0&&raw.childExitCode===73&&raw.interruptionBoundary==='after-flush-and-close-before-first-rename','OWN_CHILD_INTERRUPTION_MISSING');
 return {status:failures.length?'EXPECTED_SEMANTIC_FAILURE':'PASS',tests:raw.tests,assertions:raw.assertions,failures:raw.failures,
  buildExitCode,executeExitCode,childExitCode:raw.childExitCode};
}
export function verifyRecordIoResult(result){
 need(result?.status==='PASS'&&result.passed===true&&result.actualFunctionsVerbatim===true,'SIDECAR_NOT_COMPLETED');
 need(isDeepStrictEqual(result.functions.map(f=>f.name),FUNCTIONS)&&isDeepStrictEqual(result.sourceBindings.map(r=>r.path),INPUTS),'SOURCE_MEMBERSHIP_CHANGED');
 for(const row of result.sourceBindings)need(hash(row.snapshot)===row.sha256,'SOURCE_SNAPSHOT_CHANGED');
 const sources=Object.fromEntries(result.sourceBindings.map(r=>[r.path,r.snapshot]));
 const funcs=FUNCTIONS.map(name=>extract(fs.readFileSync(sources[name==='msiCrashReadOwnedRecord'?'native/msi_crash_recovery_runtime.h':'native/main.cpp'],'utf8'),name));
 need(isDeepStrictEqual(funcs.map(({code,...r})=>r),result.functions),'FUNCTION_RANGE_OR_HASH_CHANGED');const exact=funcs.map(f=>f.code).join('\n\n')+'\n';
 need(isDeepStrictEqual(Object.keys(result.generated),['original',...Object.keys(FAULTS)]),'GENERATED_VARIANTS_CHANGED');
 for(const [variant,row]of Object.entries(result.generated)){
  const expected=variant==='original'?exact:exact.replace(FAULTS[variant].before,()=>FAULTS[variant].after);
  need(fs.readFileSync(row.path,'utf8')===expected&&hash(row.path)===row.sha256,'EXACT_EXTRACT_OR_MUTATION_CHANGED');
 }
 need(isDeepStrictEqual(Object.keys(result.profiles),['Release','Checked']),'PROFILES_MISSING');
 for(const profile of Object.values(result.profiles)){
  need(isDeepStrictEqual(Object.keys(profile),['original',...Object.keys(FAULTS)]),'COMPILED_NEGATIVES_MISSING');
  for(const [variant,row]of Object.entries(profile)){
   for(const key of ['report','executable','dependencies','build','execute','stdout'])need(hash(row[key].path)===row[key].sha256,'ARTIFACT_CHANGED:'+key);
   const build=read(row.build.path),executed=read(row.execute.path);
   need(build.exitCode===row.buildExitCode&&executed.exitCode===row.executeExitCode&&path.resolve(executed.executable)===path.resolve(row.executable.path)&&
      executed.stdout===row.stdout.path&&!executed.error&&!executed.signal&&!build.error&&!build.signal,'COMMAND_EXECUTION_BINDING_CHANGED');
   need(isDeepStrictEqual(read(row.report.path),read(row.stdout.path)),'STDOUT_REPORT_CHANGED');
   need(isDeepStrictEqual(verifyRecordIoProfile(read(row.report.path),{variant,buildExitCode:build.exitCode,executeExitCode:executed.exitCode}),row.verification),'PROFILE_RECEIPT_CHANGED');
   const deps=read(row.dependencies.path);need(deps.Data.Includes.some(p=>path.resolve(p).toLowerCase()===path.resolve(result.generated[variant].path).toLowerCase()),'EXTRACT_NOT_ACTUALLY_COMPILED');
  }
 }
 need(result.compilerInputs.dependencyFiles===6&&result.compilerInputs.libraries>0&&result.inputHashesStable===true&&result.originalAssertionsUnchanged===true,'COMPILER_CLOSURE_MISSING');
 for(const row of result.compilerInputs.rows)need(hash(row.path)===row.sha256,'COMPILER_INPUT_CHANGED');
 need(result.boundary.privateOwnedProcessesOnly&&result.boundary.osRebootOrPowerLoss===false&&result.boundary.installedOrDeviceAcceptance===false,'OVERSTATED_SCOPE');
 return {status:'PASS',profiles:result.profiles,functions:result.functions,testsPerExecution:16,assertionsPerExecution:67,
  compiledSemanticNegatives:Object.keys(FAULTS),inputHashesStable:true};
}
export function runRecordIo({out,repo=DEFAULT_REPO,toolchainFile=path.join(repo,MODULE,'toolchain-inputs.json')}){
 need(process.platform==='win32','WINDOWS_REQUIRED');repo=path.resolve(repo);toolchainFile=path.resolve(toolchainFile);
 const root=outputRoot(out,repo,toolchainFile);fs.mkdirSync(root,{recursive:true});const steps=[];
 try{
  const snapshot=path.join(root,'in'),sourceBindings=INPUTS.map(relative=>{
   const source=path.join(repo,relative),target=path.join(snapshot,relative);fs.mkdirSync(path.dirname(target),{recursive:true});
   fs.copyFileSync(source,target,fs.constants.COPYFILE_EXCL);return {path:relative,source,snapshot:target,sha256:hash(source)};
  });
  need(hash(path.join(repo,'tools/msi_crash_record_io_selftest.mjs'))===hash(fileURLToPath(import.meta.url)),'RUNNER_DIFFERS_FROM_FROZEN_SOURCE');
  for(const rel of [MODULE+'/extend-fixture.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'])need(hash(path.join(repo,rel))===hash(path.join(DEFAULT_REPO,rel)),'IMPORTED_HELPER_DIFFERS_FROM_FROZEN_SOURCE');
  const toolchain=read(toolchainFile),toolchainHash=hash(toolchainFile);need(toolchain.files.length>0&&toolchain.files.length<=640,'TOOLCHAIN_MEMBERSHIP');
  const node=toolchain.files.find(r=>r.roles.includes('node'));need(node&&path.resolve(node.path).toLowerCase()===path.resolve(process.execPath).toLowerCase(),'NODE_NOT_PINNED');
  for(const row of toolchain.files)need(hash(row.path)===row.sha256,'TOOLCHAIN_CHANGED:'+row.path);
  write(path.join(root,'input-snapshot.json'),{sourceBindings,toolchainFile,toolchainHash,toolchain});
  const funcs=FUNCTIONS.map(name=>extract(fs.readFileSync(path.join(snapshot,name==='msiCrashReadOwnedRecord'?'native/msi_crash_recovery_runtime.h':'native/main.cpp'),'utf8'),name));
  const exact=funcs.map(f=>f.code).join('\n\n')+'\n',generated={};
  for(const variant of ['original',...Object.keys(FAULTS)]){
   const dir=path.join(root,variant);fs.mkdirSync(dir);let text=exact;
   if(variant!=='original'){need(exact.split(FAULTS[variant].before).length===2,'MUTATION_SITE_NOT_UNIQUE');text=exact.replace(FAULTS[variant].before,()=>FAULTS[variant].after);}
   const include=path.join(dir,'actual-record-functions.inc');fs.writeFileSync(include,text,{flag:'wx'});generated[variant]={path:include,sha256:hash(include),mutation:FAULTS[variant]??null};
  }
  write(path.join(root,'source-binding.json'),{sourceBindings,functions:funcs.map(({code,...r})=>r),generated,assertionsSharedByAllVariants:true,businessPredicatesRewritten:false});
  const env={...process.env};for(const key of Object.keys(env))if(/^(?:CL|_CL_|LINK|_LINK_|NODE_OPTIONS|NODE_PATH|YMCC_VCVARS64)$/i.test(key))delete env[key];
  const temp=path.join(root,'t');fs.mkdirSync(temp);Object.assign(env,{TEMP:temp,TMP:temp,VSCMD_SKIP_SENDTELEMETRY:'1'});
  const quote=p=>{need(!/["%\r\n]/.test(p),'UNSAFE_COMPILER_PATH');return '"'+p+'"';};
  const preamble=calibrationToolchainPreamble(toolchain.vcvars64),profiles={},compilerRows=[],dependencyFiles=[];
  const artifacts=new Map(toolchain.files.map(r=>[path.resolve(r.path).toLowerCase(),r.sha256]));
  function bind(file,kind){const resolved=path.resolve(file),sha256=hash(resolved);need(inside(root,resolved)||artifacts.get(resolved.toLowerCase())===sha256,'UNPINNED_COMPILER_INPUT:'+resolved);compilerRows.push({path:resolved,sha256,kind});}
  function execute(label,exe,args,cwd,timeout){
   const p=spawnSync(exe,args,{cwd,env,windowsHide:true,encoding:'utf8',timeout,maxBuffer:8*1024*1024});
   const stdout=path.join(cwd,label+'.stdout.log'),stderr=path.join(cwd,label+'.stderr.log');fs.writeFileSync(stdout,p.stdout??'');fs.writeFileSync(stderr,p.stderr??'');
   const command={executable:exe,args,cwd,exitCode:p.status,signal:p.signal,error:p.error?.message??null,stdout,stderr};write(path.join(cwd,label+'.command.json'),command);steps.push(command);
   need(!p.error&&!p.signal&&Number.isInteger(p.status),'PROCESS_INCOMPLETE:'+label);return p;
  }
  let libraries=0;
  for(const profile of ['Release','Checked']){
   profiles[profile]={};for(const variant of Object.keys(generated)){
    const dir=path.join(root,variant,profile);fs.mkdirSync(dir);const exe=path.join(dir,'selftest.exe'),depfile=path.join(dir,'selftest.cpp.json');
    const flags=['/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/permissive-','/sdl','/Zc:__cplusplus',
     ...(profile==='Release'?['/O2','/MT','/DNDEBUG']:['/Od','/MTd','/RTC1','/Zi','/D_DEBUG']),
     '/I'+quote(path.dirname(generated[variant].path)),'/external:I'+quote(path.join(snapshot,'deps/json')),'/external:W0',
     '/showIncludes','/sourceDependencies',quote(depfile),quote(path.join(snapshot,'tools/msi_crash_record_io_selftest.cpp')),
     '/Fo'+quote(path.join(dir,'selftest.obj')),'/Fe'+quote(exe),'/Fd'+quote(path.join(dir,'selftest.pdb')),
     '/link','/verbose:lib','/SUBSYSTEM:CONSOLE','/MACHINE:x64','/INCREMENTAL:NO'];
    const batch=path.join(dir,'build.cmd');fs.writeFileSync(batch,preamble+'where cl\r\nwhere link\r\ncl '+flags.join(' ')+'\r\nexit /b %errorlevel%\r\n',{flag:'wx'});
    const built=execute('build',path.join(process.env.SystemRoot||'C:/Windows','System32/cmd.exe'),['/d','/c',batch],dir,60000);need(built.status===0,'BUILD_FAILED:'+profile+'/'+variant);
    const deps=read(depfile);need(Array.isArray(deps.Data?.Includes),'SOURCE_DEPENDENCIES_MISSING');dependencyFiles.push(depfile);bind(deps.Data.Source,'Compile');for(const p of deps.Data.Includes)bind(p,'Include');
    need(deps.Data.Includes.some(p=>path.resolve(p).toLowerCase()===path.resolve(generated[variant].path).toLowerCase()),'EXTRACT_NOT_ACTUALLY_COMPILED');
    for(const match of built.stdout.matchAll(/([A-Za-z]:\\[^\r\n]*?\.lib)/g))if(fs.existsSync(match[1])){bind(match[1],'Library');++libraries;}
    const executed=execute('execute',exe,[],dir,30000);let raw;try{raw=JSON.parse(executed.stdout);}catch{throw Error('MSI_RECORD_IO_REPORT_MISSING:'+profile+'/'+variant);}
    const report=path.join(dir,'results.json');write(report,raw);const verification=verifyRecordIoProfile(raw,{variant,buildExitCode:built.status,executeExitCode:executed.status});
    const artifact=p=>({path:p,sha256:hash(p)});profiles[profile][variant]={buildExitCode:built.status,executeExitCode:executed.status,verification,
     report:artifact(report),executable:artifact(exe),dependencies:artifact(depfile),build:artifact(path.join(dir,'build.command.json')),
     execute:artifact(path.join(dir,'execute.command.json')),stdout:artifact(path.join(dir,'execute.stdout.log'))};
   }
  }
  need(libraries>0,'LIBRARY_CLOSURE_MISSING');for(const row of sourceBindings)need(hash(row.source)===row.sha256&&hash(row.snapshot)===row.sha256,'SOURCE_CHANGED:'+row.path);
  for(const row of toolchain.files)need(hash(row.path)===row.sha256,'TOOL_CHANGED:'+row.path);need(hash(toolchainFile)===toolchainHash,'TOOLCHAIN_DESCRIPTOR_CHANGED');
  const compilerInputs={dependencyFiles:dependencyFiles.length,libraries,rows:compilerRows};write(path.join(root,'evaluated-compiler-inputs.json'),compilerInputs);write(path.join(root,'process-ledger.json'),{steps});
  const result={schemaVersion:1,status:'PASS',passed:true,actualFunctionsVerbatim:true,functions:funcs.map(({code,...r})=>r),sourceBindings,profiles,generated,compilerInputs,
   originalAssertionsUnchanged:true,inputHashesStable:true,toolchainArtifacts:toolchain.files.length,productBugObserved:false,
   boundary:{privateRealFilesystem:true,privateOwnedProcessesOnly:true,oneOwnedChildAtATime:true,writerInterruptedAfterFlushCloseBeforeRename:true,
    osRebootOrPowerLoss:false,orphanTempRetained:true,explicitErrorLeaves:['write-failure','short-write','flush-failure','rename-failure','move-only-fallback','read-failure','short-read','changed-info'],
    installedOrDeviceAcceptance:false,productOrSteamRun:false,devicesServicesHidHide:false,deploymentOrExport:false}};
  verifyRecordIoResult(result);write(path.join(root,'result.json'),result);return result;
 }catch(error){write(path.join(root,'failure.json'),{status:'FAIL',reason:error.message,steps,compileFailureAcceptedAsNegative:false});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const args=process.argv.slice(2),options={},keys={'--out':'out','--repo':'repo','--toolchain':'toolchainFile'};need(args.length>=2&&args.length%2===0,'ARGUMENTS');
  for(let i=0;i<args.length;i+=2){need(Object.hasOwn(keys,args[i])&&!Object.hasOwn(options,keys[args[i]]),'ARGUMENTS');options[keys[args[i]]]=args[i+1];}
  const result=runRecordIo(options);console.log(JSON.stringify({status:result.status,out:options.out,profiles:result.profiles,functions:result.functions,compilerDependencies:result.compilerInputs.dependencyFiles,productBugObserved:result.productBugObserved}));
 }catch(error){console.error(error.stack??error.message);process.exitCode=2;}
}
