// Actual MSVC evaluated-input evidence for the existing motion producer.
// A declared prerequisite is never claimed as a header/library observed by MSVC.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {motionConsumerInputs} from '../ymcc_sim/global/motion-consumer-signals/inputs.mjs';
import {MATRIX_TOOL_MANIFESTS} from '../ymcc_sim/global/motion-consumer-signals/matrix-evidence.mjs';
export const TRACE_HELPER='tools/lib/hc_motion_compiler_trace.mjs';
export const TRACE_VERIFIER='tools/ymcc_sim/global/motion-consumer-signals/original-compiler-trace.mjs';
export const TRACE_SCHEMA='hc-motion-actual-compiler-inputs-v1';
export const GENERATED_NAMES=Object.freeze(['motion_consumer.cpp','hc_model_motion_catalog.snapshot.h','hc_legacy_motion_catalog.snapshot.h','msi_motion_capabilities.snapshot.h','hc_generic_motion_catalog.h','hc_default_motion_catalog.h','hc_oem_windows_motion_catalog.h','unified_binding.cpp','HcMotionCapabilityCatalog.h','HcMotionCapabilityCatalog.generated.h']);
const need=(ok,code,evidence)=>{if(!ok)throw Object.assign(Error(code),{evidence});};
export const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
export const fileHash=file=>digest(fs.readFileSync(file));
const validHash=value=>typeof value==='string'&&/^[a-f0-9]{64}$/.test(value);
const key=file=>path.resolve(file).toLowerCase();
const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const read=file=>{need(fs.statSync(file).size<=16*1024*1024,'MOTION_TRACE_JSON_LIMIT');return JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));};
function exactFile(file,hash){
 need(typeof file==='string'&&path.isAbsolute(file)&&!/[\0\r\n]/.test(file)&&validHash(hash),'MOTION_TRACE_FILE_IDENTITY');
 let cursor=path.resolve(file);for(;;){need(!fs.lstatSync(cursor).isSymbolicLink(),'MOTION_TRACE_REPARSE_FILE',{file});const parent=path.dirname(cursor);if(cursor===parent)break;cursor=parent;}
 need(fs.statSync(file).isFile()&&fileHash(file)===hash,'MOTION_TRACE_FILE_CHANGED',{file,expected:hash});return file;
}
function inside(root,file){const rel=path.relative(path.resolve(root),path.resolve(file));return rel!==''&&!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep);}
export function traceFile(root,relative){need(typeof relative==='string'&&relative&&!path.isAbsolute(relative)&&!relative.split(/[\\/]/).includes('..'),'MOTION_TRACE_RELATIVE_FILE');const file=path.resolve(root,relative);need(inside(root,file),'MOTION_TRACE_FILE_ESCAPE');return file;}
export function traceRef(root,file){need(inside(root,file),'MOTION_TRACE_REFERENCE_ESCAPE');return {path:path.relative(root,file).replaceAll('\\','/'),sha256:fileHash(file)};}
function saveFresh(file,value){fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});return file;}
function rowsMap(rows){const result=new Map();for(const row of rows){need(row&&typeof row.path==='string'&&path.isAbsolute(row.path)&&validHash(row.sha256),'MOTION_TRACE_DECLARATION_ROW');const prev=result.get(key(row.path));need(!prev||prev.sha256===row.sha256,'MOTION_TRACE_DECLARATION_CONFLICT');result.set(key(row.path),row);}return result;}
const inputRows=report=>['source','before','historical','capabilityBefore','legacyCatalog','legacyOracle','catalog','msi','runner'].map(n=>report[n]).concat(report.fixtures??[]);
const inputRecord=(file,role)=>({path:path.resolve(file),sha256:fileHash(file),role});
function archiveRoot(out){return path.basename(out)==='evidence'&&path.basename(path.dirname(path.dirname(out)))==='mutants'?path.dirname(path.dirname(path.dirname(out))):out;}
function snapshotSource(out,row){const relative='trace-source-bytes/'+row.sha256+'.bin',saved=traceFile(archiveRoot(out),relative);fs.mkdirSync(path.dirname(saved),{recursive:true});if(!fs.existsSync(saved)){const bytes=fs.readFileSync(row.path);need(digest(bytes)===row.sha256,'MOTION_TRACE_SNAPSHOT_SOURCE_CHANGED');const temp=saved+'.'+process.pid+'.tmp';fs.writeFileSync(temp,bytes,{flag:'wx'});try{fs.renameSync(temp,saved);}catch(error){if(!fs.existsSync(saved))throw error;fs.unlinkSync(temp);}}exactFile(saved,row.sha256);return {...row,saved:relative};}
function authorizedMutationRows(out,report){
 if(path.basename(out)!=='evidence')return [];
 const parent=path.dirname(out),name=path.basename(parent);need(/^[a-z0-9-]+$/.test(name)&&path.basename(path.dirname(parent))==='mutants','MOTION_TRACE_MUTANT_LOCATION');
 const records=[['source','main.mutant.cpp'],['catalog','catalog.mutant.h'],['legacyCatalog','legacy-catalog.mutant.h']].map(([field,filename])=>{const row=report[field];need(row&&key(row.path)===key(path.join(parent,filename)),'MOTION_TRACE_MUTANT_INPUT');return {...row,role:'owned-mutant-input'};});
 const binding=(report.fixtures??[]).find(r=>key(r.path)===key(path.join(parent,'binding.mutant.h')));
 if(binding){need(name==='legacy-controller-windows-admitted','MOTION_TRACE_UNKNOWN_BINDING_FAULT');records.push({...binding,role:'owned-mutant-input'});}return records;
}
/** Called at each original generation write, before any compiler can run. */
export function writeMotionGenerated(out,name,bytes,records){
 need(GENERATED_NAMES.includes(name)&&!records.some(row=>row.path===path.join(out,name)),'MOTION_TRACE_GENERATED_NAME');
 const file=path.join(out,name),buffer=Buffer.isBuffer(bytes)?bytes:Buffer.from(bytes);fs.writeFileSync(file,buffer,{flag:'wx'});
 const row={path:file,sha256:digest(buffer),role:'owned-generated-at-creation'};records.push(row);return file;
}
export function captureTraceAuthority(repo,out,report,generatedRows){
 const inputs=motionConsumerInputs(repo);need(!inputs.oracleProblem&&!inputs.toolchainProblem&&inputs.tools,'MOTION_TRACE_REQUIRED_AUTHORITY');
 const sources=inputs.sources.map(name=>inputRecord(path.join(repo,name),'declared-source'));
 const declaredTools=inputs.tools.files.map(row=>({...row,role:'declared-tool-prerequisite'}));need(declaredTools.length>=574,'MOTION_TRACE_ALL_TOOLS_REQUIRED');
 const toolKeys=new Set(declaredTools.map(row=>key(row.path)));
 const historical=inputs.artifacts.filter(file=>!toolKeys.has(key(file))).map(file=>inputRecord(file,'declared-historical-input'));
 const owned=authorizedMutationRows(out,report),allowed=rowsMap([...sources,...historical,...declaredTools,...owned]);
 for(const row of inputRows(report))need(row&&allowed.get(key(row.path))?.sha256===row.sha256,'MOTION_TRACE_UNDECLARED_PRODUCER_INPUT',{file:row?.path});
 need(generatedRows.length===GENERATED_NAMES.length&&GENERATED_NAMES.every(name=>generatedRows.some(row=>key(row.path)===key(path.join(out,name)))),'MOTION_TRACE_GENERATED_SET');
 for(const row of [...sources,...historical,...declaredTools,...owned,...generatedRows])exactFile(row.path,row.sha256);
 const cpp=generatedRows.find(r=>path.basename(r.path)==='motion_consumer.cpp');need(cpp.sha256===report.generatedCppSha256,'MOTION_TRACE_CPP_SOURCE_DIFFERS');
 // Content-addressed source copies are shared between the two configurations of
 // this producer. Tools are pinned completely; only observed rows count as used.
 const snapshots=[...sources,...historical,...owned,...generatedRows].map(row=>snapshotSource(out,row));
 const authority={schema:TRACE_SCHEMA,kind:'predeclared-exact-input-authority',repo:path.resolve(repo),out:path.resolve(out),archiveRoot:path.resolve(archiveRoot(out)),producerSource:{...report.source},producerRunner:{...report.runner},sourceRows:snapshots,toolRows:declaredTools,generatedRows,manifestRows:MATRIX_TOOL_MANIFESTS.map(n=>inputRecord(path.join(repo,n),'tool-manifest')),declarationIsNotObservedGraph:true,osModuleLoadingProven:false};
 const declaration=saveFresh(path.join(out,'compiler-declaration.json'),authority);
 return {authority,ref:traceRef(out,declaration),tools:inputs.tools};
}
export function validateProcessProof(proof,allowedExits){
 need(proof&&Number.isSafeInteger(proof.pid)&&proof.pid>0&&allowedExits.includes(proof.exitCode)&&proof.signal===null&&proof.timedOut===false&&proof.launchError===null&&proof.error===null&&proof.outputTruncated===false&&proof.naturalExitObserved===true&&Number.isFinite(proof.durationMs)&&proof.durationMs>=0&&proof.timeoutMs===180000&&proof.outputLimit===32*1024*1024,'MOTION_TRACE_NATURAL_PROCESS_REQUIRED');
}
function resolveExecutable(command,env){
 if(path.isAbsolute(command))return path.resolve(command);
 const actualPath=Object.entries(env).find(([k])=>k.toLowerCase()==='path')?.[1]??'';
 for(const directory of actualPath.split(path.delimiter)){if(!directory)continue;const file=path.join(directory,command);if(fs.existsSync(file)&&fs.statSync(file).isFile())return path.resolve(file);}throw Error('MOTION_TRACE_EXECUTABLE_NOT_FOUND');
}
/** No environment values are written; stdout/stderr retain their complete bytes. */
export function captureMotionProcess({out,log,command,argv,env,cwd,raw,startedAt}){
 need(/^(?:Release|Checked)\/(?:compile|run)\.log$/.test(log),'MOTION_TRACE_PROCESS_LOG_NAME');
 let executable=null;try{executable=resolveExecutable(command,env);}catch{}const stdout=Buffer.isBuffer(raw.stdout)?raw.stdout:Buffer.from(raw.stdout??''),stderr=Buffer.isBuffer(raw.stderr)?raw.stderr:Buffer.from(raw.stderr??'');
 const stdoutFile=path.join(out,log+'.stdout.bin'),stderrFile=path.join(out,log+'.stderr.bin');fs.writeFileSync(stdoutFile,stdout,{flag:'wx'});fs.writeFileSync(stderrFile,stderr,{flag:'wx'});
 const error=raw.error?{code:raw.error.code??null,message:raw.error.message}:null;
 let executableSha256=null;try{if(executable)executableSha256=fileHash(executable);}catch{}
 const proof={schema:TRACE_SCHEMA,executable,executableSha256,args:argv,cwd:path.resolve(cwd),pid:raw.pid??null,exitCode:raw.status,signal:raw.signal??null,timedOut:raw.error?.code==='ETIMEDOUT',launchError:raw.error&&['ENOENT','EACCES','EPERM'].includes(raw.error.code)?raw.error.code:null,error,outputTruncated:['ENOBUFS','ERR_CHILD_PROCESS_STDIO_MAXBUFFER'].includes(raw.error?.code),naturalExitObserved:Number.isInteger(raw.status)&&raw.signal===null&&!raw.error,timeoutMs:180000,outputLimit:32*1024*1024,durationMs:Date.now()-startedAt,stdout:traceRef(out,stdoutFile),stderr:traceRef(out,stderrFile)};
 const file=saveFresh(path.join(out,log+'.process.json'),proof);return {proof,reference:traceRef(out,file)};
}
export function beginMotionCompile({out,name,argv,env,authority}){
 need(['Release','Checked'].includes(name),'MOTION_TRACE_PROFILE');
 for(const [k,v] of Object.entries(env))if(/^(?:CL|_CL_|LINK|_LINK_)$/i.test(k))need(!v,'MOTION_TRACE_IMPLICIT_COMPILER_OPTIONS',{name:k});
 for(const row of [...authority.authority.sourceRows,...authority.authority.toolRows])exactFile(row.path,row.sha256);
 const deps=path.join(out,name,'source-dependencies.json');
 need(argv.includes('/sourceDependencies')&&argv[argv.indexOf('/sourceDependencies')+1]===deps&&argv.includes('/link')&&argv.includes('/verbose:lib'),'MOTION_TRACE_REQUIRED_FLAGS');
 const value={schema:TRACE_SCHEMA,kind:'before-actual-compile',configuration:name,authority:authority.ref,sourceRows:authority.authority.sourceRows,toolRows:authority.authority.toolRows,generatedRows:authority.authority.generatedRows,command:['cl.exe',...argv],cwd:path.join(out,name),createdAt:new Date().toISOString(),declarationIsNotObservedGraph:true};
 const file=saveFresh(path.join(out,name,'compiler-inputs.before.json'),value);return {value,ref:traceRef(out,file),deps};
}
function decode(bytes){try{return {encoding:'utf-8',text:new TextDecoder('utf-8',{fatal:true}).decode(bytes)};}catch{return {encoding:'gb18030',text:new TextDecoder('gb18030',{fatal:true}).decode(bytes)};}}
/** Preserve line order and repeated path occurrences. This records linker
 * searches/references; it cannot establish OS loader/module runtime behavior. */
export function parseMotionLibraries(stdout,stderr){
 const rows=[],encodings={};
 for(const [stream,bytes] of [['stdout',stdout],['stderr',stderr]]){
  const decoded=decode(bytes);encodings[stream]=decoded.encoding;let context=null;
  for(const [index,line] of decoded.text.split(/\r?\n/).entries()){
   const matches=[...line.matchAll(/[A-Za-z]:[\\/][^\r\n]*?\.lib\b/gi)];
   for(const [occurrence,match] of matches.entries()){
    need(path.isAbsolute(match[0]),'MOTION_TRACE_LIBRARY_ABSOLUTE');const file=path.resolve(match[0]);rows.push({stream,line:index+1,occurrence,path:file,role:'actual-link-library-search'});context=file;
   }
   if(!matches.length&&/\.lib\b/i.test(line)){
    // MSVC emits "Loaded NAME.lib(member.obj)" after a full search path.
    const loaded=/^\s*(?:Loaded|已加载|已載入)\s+([^\\/:\r\n]+\.lib)\([^\r\n]+\)\s*$/i.exec(line);
    if(loaded){need(context&&path.basename(context).toLowerCase()===loaded[1].toLowerCase(),'MOTION_TRACE_UNRESOLVED_LIBRARY_REFERENCE',{line});rows.push({stream,line:index+1,occurrence:0,path:context,role:'actual-link-library-reference'});}
    else throw Object.assign(Error('MOTION_TRACE_UNRESOLVED_LIBRARY_LINE'),{evidence:{stream,line:index+1,text:line}});
   }
  }
 }
 need(rows.some(row=>row.role==='actual-link-library-search'),'MOTION_TRACE_ACTUAL_LIBRARIES_MISSING');return {rows,encodings};
}
export function evaluateMotionTrace({dependency,stdout,stderr,source,sourceRows,toolRows,generatedRows}){
 need(dependency&&typeof dependency.Data?.Source==='string'&&key(dependency.Data.Source)===key(source)&&Array.isArray(dependency.Data.Includes)&&dependency.Data.Includes.length>0,'MOTION_TRACE_ACTUAL_HEADERS_REQUIRED');
 const allowed=rowsMap([...sourceRows,...toolRows,...generatedRows]);
 const authorize=(file,role)=>{need(typeof file==='string'&&path.isAbsolute(file),'MOTION_TRACE_ACTUAL_INPUT_PATH');const row=allowed.get(key(file));need(row,'MOTION_TRACE_UNKNOWN_ACTUAL_INPUT',{file,role,actualSha256:fs.existsSync(file)&&fs.statSync(file).isFile()?fileHash(file):null});exactFile(file,row.sha256);return {path:file,sha256:row.sha256,role};};
 const headers=[authorize(dependency.Data.Source,'actual-compile-source'),...dependency.Data.Includes.map(file=>authorize(file,'actual-header'))];
 const linked=parseMotionLibraries(stdout,stderr),libraries=linked.rows.map(row=>({...row,...authorize(row.path,row.role)}));
 return {headers,libraries,encodings:linked.encodings,scope:'Actual MSVC sourceDependencies and linker library searches/references; not an OS module/load graph.'};
}
function referenced(root,ref){need(ref&&validHash(ref.sha256),'MOTION_TRACE_REFERENCE_REQUIRED');const file=traceFile(root,ref.path);exactFile(file,ref.sha256);return file;}
function finishCompileProof({out,name,argv,before,processRecord,authority}){
 const processProof=processRecord.proof;validateProcessProof(processProof,[0]);
 const tools=rowsMap(authority.authority.toolRows);need(path.basename(processProof.executable).toLowerCase()==='cl.exe'&&tools.get(key(processProof.executable))?.sha256===processProof.executableSha256,'MOTION_TRACE_COMPILER_NOT_DECLARED');
 const dependencyFile=before.deps;need(fs.existsSync(dependencyFile),'MOTION_TRACE_DEPENDENCY_FILE_MISSING');
 const observed=evaluateMotionTrace({dependency:read(dependencyFile),stdout:fs.readFileSync(referenced(out,processProof.stdout)),stderr:fs.readFileSync(referenced(out,processProof.stderr)),source:path.join(out,'motion_consumer.cpp'),sourceRows:authority.authority.sourceRows,toolRows:authority.authority.toolRows,generatedRows:authority.authority.generatedRows});
 for(const row of authority.authority.sourceRows)exactFile(row.path,row.sha256);
 const value={schema:TRACE_SCHEMA,status:'PASS',configuration:name,before:before.ref,authority:authority.ref,compilerProcess:processRecord.reference,dependencies:traceRef(out,dependencyFile),command:['cl.exe',...argv],...observed};
 const file=saveFresh(path.join(out,name,'compiler-trace.json'),value);return {status:'PASS',...traceRef(out,file)};
}
export function finishMotionCompile(options){
 try{return finishCompileProof(options);}catch(error){
  saveFresh(path.join(options.out,options.name,'compiler-trace.failure.json'),{schema:TRACE_SCHEMA,status:'FAIL',configuration:options.name,code:error.message,evidence:error.evidence??null,before:options.before.ref,compilerProcess:options.processRecord.reference,actualDependencyFile:options.before.deps});throw error;
 }
}
export function requireMotionNativeProcess(out,processRecord,exe){validateProcessProof(processRecord.proof,[0,1]);need(key(processRecord.proof.executable)===key(exe)&&processRecord.proof.executableSha256===fileHash(exe),'MOTION_TRACE_NATIVE_EXECUTABLE_CHANGED');return processRecord.reference;}
export function verifyMotionTraceConfiguration({repo,out,report,config,expectedAuthority}){
 const trace=read(referenced(out,config.compilerTrace));need(config.compilerTrace.status==='PASS'&&trace.schema===TRACE_SCHEMA&&trace.status==='PASS'&&trace.configuration===config.name&&same(trace.command,config.compileCommand),'MOTION_TRACE_CONFIGURATION_BINDING');
 const before=read(referenced(out,trace.before)),authority=read(referenced(out,trace.authority));
 need(same(trace.authority,report.compilerTrace?.declaration)&&same(before.authority,trace.authority)&&before.configuration===config.name&&before.kind==='before-actual-compile'&&same(before.command,config.compileCommand),'MOTION_TRACE_BEFORE_BINDING');
 need(authority.schema===TRACE_SCHEMA&&authority.kind==='predeclared-exact-input-authority'&&key(authority.repo)===key(repo)&&key(authority.out)===key(out)&&key(authority.archiveRoot??'')===key(archiveRoot(out))&&same(authority.producerSource,report.source)&&same(authority.producerRunner,report.runner)&&authority.declarationIsNotObservedGraph===true&&authority.osModuleLoadingProven===false,'MOTION_TRACE_AUTHORITY_BINDING');
 need(same(authority.sourceRows,expectedAuthority.sourceRows)&&same(authority.toolRows,expectedAuthority.toolRows)&&same(authority.generatedRows,report.generatedInputsAtCreation)&&same(before.sourceRows,authority.sourceRows)&&same(before.toolRows,authority.toolRows)&&same(before.generatedRows,authority.generatedRows),'MOTION_TRACE_AUTHORITY_SET_DIFFERS');
 for(const row of authority.sourceRows){exactFile(row.path,row.sha256);exactFile(traceFile(archiveRoot(out),row.saved),row.sha256);}for(const row of authority.toolRows)exactFile(row.path,row.sha256);
 const compile=read(referenced(out,trace.compilerProcess));validateProcessProof(compile,[0]);need(same(compile.args,config.compileCommand.slice(1))&&key(compile.cwd)===key(path.join(out,config.name)),'MOTION_TRACE_COMPILER_PROCESS_COMMAND');
 const tools=rowsMap(authority.toolRows);need(path.basename(compile.executable).toLowerCase()==='cl.exe'&&tools.get(key(compile.executable))?.sha256===compile.executableSha256,'MOTION_TRACE_COMPILER_IDENTITY');exactFile(compile.executable,compile.executableSha256);
 const dependencyFile=referenced(out,trace.dependencies);need(key(dependencyFile)===key(path.join(out,config.name,'source-dependencies.json'))&&config.compileCommand.includes('/sourceDependencies')&&key(config.compileCommand[config.compileCommand.indexOf('/sourceDependencies')+1])===key(dependencyFile)&&config.compileCommand.includes('/verbose:lib'),'MOTION_TRACE_COMPILE_FLAGS_BINDING');
 const compileStdout=fs.readFileSync(referenced(out,compile.stdout)),compileStderr=fs.readFileSync(referenced(out,compile.stderr));
 need(compileStdout.length<=compile.outputLimit&&compileStderr.length<=compile.outputLimit&&fs.readFileSync(path.join(out,config.name,'compile.log'),'utf8')===compileStdout.toString('utf8')+compileStderr.toString('utf8'),'MOTION_TRACE_COMPLETE_COMPILER_OUTPUT');
 const evaluated=evaluateMotionTrace({dependency:read(dependencyFile),stdout:fs.readFileSync(referenced(out,compile.stdout)),stderr:fs.readFileSync(referenced(out,compile.stderr)),source:path.join(out,'motion_consumer.cpp'),sourceRows:authority.sourceRows,toolRows:authority.toolRows,generatedRows:authority.generatedRows});
 need(same(evaluated.headers,trace.headers)&&same(evaluated.libraries,trace.libraries)&&same(evaluated.encodings,trace.encodings),'MOTION_TRACE_ACTUAL_GRAPH_DIFFERS');
 const native=read(referenced(out,config.nativeProcess));validateProcessProof(native,[config.exitCode]);need(key(native.executable)===key(path.join(out,config.name,'motion_consumer.exe'))&&native.executableSha256===config.executableSha256&&same(native.args,[path.join(out,'expectations.json')])&&key(native.cwd)===key(path.join(out,config.name)),'MOTION_TRACE_NATIVE_PROCESS_BINDING');exactFile(native.executable,native.executableSha256);
 const stdout=fs.readFileSync(referenced(out,native.stdout)),stderr=fs.readFileSync(referenced(out,native.stderr));need(stdout.length<=native.outputLimit&&stderr.length<=native.outputLimit&&fs.readFileSync(path.join(out,config.name,'run.log'),'utf8')===stdout.toString('utf8')+stderr.toString('utf8')&&stderr.length===0&&same(JSON.parse(stdout.toString('utf8').trim()),config.report),'MOTION_TRACE_NATIVE_OUTPUT_DIFFERS');
 return {configuration:config.name,trace:config.compilerTrace,headerOccurrences:trace.headers.length,libraryOccurrences:trace.libraries.length,compileExit:compile.exitCode,nativeExit:native.exitCode};
}
/** The independent verifier rebuilds exact declarations from pre-run public pins. */
export function expectedMotionTraceAuthority({repo,out,report,sourceInputs,externalInputs,toolInputs}){
 const tools=rowsMap(toolInputs),base=[...sourceInputs.map(r=>({path:path.resolve(r.path),sha256:r.sha256,role:'declared-source'})),...externalInputs.filter(r=>!tools.has(key(r.path))).map(r=>({path:path.resolve(r.path),sha256:r.sha256,role:'declared-historical-input'}))];
 const owned=authorizedMutationRows(out,report),allowed=rowsMap([...base,...toolInputs,...owned]);for(const row of inputRows(report))need(row&&allowed.get(key(row.path))?.sha256===row.sha256,'MOTION_TRACE_PUBLIC_UNDECLARED_INPUT');
 need(Array.isArray(report.generatedInputsAtCreation)&&report.generatedInputsAtCreation.length===GENERATED_NAMES.length,'MOTION_TRACE_GENERATION_AUTHORITY_MISSING');
 for(const name of GENERATED_NAMES){const row=report.generatedInputsAtCreation.find(r=>key(r.path)===key(path.join(out,name)));need(row&&row.role==='owned-generated-at-creation','MOTION_TRACE_GENERATION_NAME');exactFile(row.path,row.sha256);}
 need(report.generatedInputsAtCreation.find(r=>path.basename(r.path)==='motion_consumer.cpp').sha256===report.generatedCppSha256,'MOTION_TRACE_GENERATION_CPP');
 const sourceRows=[...base,...owned,...report.generatedInputsAtCreation].map(row=>({...row,saved:'trace-source-bytes/'+row.sha256+'.bin'}));
 return {sourceRows,toolRows:toolInputs.map(row=>({...row,role:'declared-tool-prerequisite'}))};
}