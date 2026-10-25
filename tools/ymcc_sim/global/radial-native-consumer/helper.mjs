import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';

export const MODULE='tools/ymcc_sim/global/radial-native-consumer';
export const CASE_ID='controller.radial-native-consumer';
export const need=(value,message)=>{if(!value)throw new Error(message);};
export const sha256=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
export const hashFile=file=>sha256(fs.readFileSync(file));
export const key=file=>path.resolve(file).toLowerCase();
export function readJson(file){need(fs.statSync(file).size<=16*1024*1024,'RADIAL_JSON_SIZE_LIMIT');return JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));}
export function writeNew(file,value){fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});}
export function inside(root,file){const relative=path.relative(path.resolve(root),path.resolve(file));return relative===''||(!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep));}
export function owned(root,file){need(inside(root,file)&&key(root)!==key(file),'RADIAL_OUTPUT_OUTSIDE_OWNED_ROOT');return path.resolve(file);}
export function evidencePath(value,repo){need(typeof value==='string'&&path.isAbsolute(value)&&!/[\0\r\n]/.test(value),'RADIAL_EVIDENCE_ABSOLUTE_REQUIRED');const output=path.resolve(value);need(!inside(repo,output)&&!inside(output,repo)&&key(output)!==key(path.parse(output).root),'RADIAL_EVIDENCE_OUTSIDE_SOURCE_REQUIRED');return output;}
export function repositoryFile(repo,relative){need(typeof relative==='string'&&relative.length>0&&!path.isAbsolute(relative)&&!relative.split(/[\\/]/).includes('..')&&!/[\0\r\n]/.test(relative),'RADIAL_SOURCE_PATH_INVALID');const file=path.resolve(repo,relative);need(inside(repo,file)&&inside(fs.realpathSync(repo),fs.realpathSync(file))&&fs.lstatSync(file).isFile()&&!fs.lstatSync(file).isSymbolicLink(),'RADIAL_SOURCE_NOT_REGULAR_OR_OUTSIDE_REPO');return file;}
export function walkFiles(root){need(fs.lstatSync(root).isDirectory()&&!fs.lstatSync(root).isSymbolicLink(),'RADIAL_DIRECTORY_INVALID');const output=[];for(const entry of fs.readdirSync(root,{withFileTypes:true})){need(!entry.isSymbolicLink(),'RADIAL_REPARSE_MEMBER');const file=path.join(root,entry.name);if(entry.isDirectory())output.push(...walkFiles(file));else{need(entry.isFile(),'RADIAL_NONREGULAR_MEMBER');output.push(file);}need(output.length<10000,'RADIAL_DIRECTORY_BUDGET');}return output.sort((a,b)=>key(a).localeCompare(key(b)));}
export function exactMembers(actual,declared,count){const a=actual.map(key).sort(),d=declared.map(key).sort();need(a.length===count&&d.length===count&&new Set(a).size===count&&JSON.stringify(a)===JSON.stringify(d),'RADIAL_TOOL_DIRECTORY_MEMBERSHIP_CHANGED');return true;}
export function validateToolManifest(manifest){
 need(manifest?.schemaVersion===1&&manifest.kind==='ymcc-radial-native-selected-toolchain'&&manifest.caseId===CASE_ID&&manifest.maxArtifactInputs===3072,'RADIAL_TOOL_SCHEMA');
 need(manifest.coreFiles===533&&manifest.powershellFiles===658&&manifest.files?.length===1191&&manifest.directories?.length===4,'RADIAL_TOOL_COMPLETE_COUNTS');
 const byPath=new Map();for(const row of manifest.files){need(path.isAbsolute(row.path)&&!/[\0\r\n]/.test(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256),'RADIAL_TOOL_MEMBER_SCHEMA');need(!byPath.has(key(row.path)),'RADIAL_TOOL_DUPLICATE_MEMBER');byPath.set(key(row.path),row);}
 for(const name of ['node','cl','link','powershell']){const row=manifest.executables?.[name];need(row&&byPath.get(key(row.path))?.sha256===row.sha256&&path.extname(row.path).toLowerCase()==='.exe','RADIAL_EXECUTABLE_NOT_DECLARED:'+name);}
 need(new Set(manifest.directories.map(d=>key(d.path))).size===4,'RADIAL_DUPLICATE_DIRECTORY');
 need(JSON.stringify(manifest.directories.map(d=>d.members).sort((a,b)=>a-b))===JSON.stringify([66,91,361,658]),'RADIAL_TOOL_DIRECTORY_COUNTS');
 for(const dir of manifest.directories){need(path.isAbsolute(dir.path)&&dir.membership==='complete-recursive-files-no-reparse','RADIAL_TOOL_DIRECTORY_SCHEMA');const rows=manifest.files.filter(f=>key(f.path).startsWith(key(dir.path)+path.sep));need(rows.length===dir.members,'RADIAL_DECLARED_DIRECTORY_MEMBERS_MISSING');}
 need(Array.isArray(manifest.includeDirs)&&manifest.includeDirs.length===2&&Array.isArray(manifest.libDirs)&&manifest.libDirs.length===3,'RADIAL_COMPILER_SEARCH_PATH_SCHEMA');
 return {manifest,byPath};
}
export function verifyToolDeclaration(manifest){
 validateToolManifest(manifest);
 const files=manifest.files.map(row=>{need(fs.lstatSync(row.path).isFile()&&!fs.lstatSync(row.path).isSymbolicLink(),'RADIAL_TOOL_NOT_REGULAR:'+row.path);const actualSha256=hashFile(row.path);need(actualSha256===row.sha256,'RADIAL_TOOL_HASH_CHANGED:'+row.path);return {path:row.path,sha256:actualSha256};});
 const directories=manifest.directories.map(dir=>{const actual=walkFiles(dir.path),declared=manifest.files.filter(f=>inside(dir.path,f.path)).map(f=>f.path);exactMembers(actual,declared,dir.members);return {path:dir.path,members:actual.length,complete:true};});
 return {status:'PASS',verifiedFiles:files.length,completeDirectories:directories.length,files,directories,selectedPackageIdentityOnly:true,osBinaryClosureClaimed:false};
}
export function captureSources(repo,root,relatives){return [...new Set(relatives)].sort().map(relative=>{const original=repositoryFile(repo,relative),bytes=fs.readFileSync(original),sha=sha256(bytes),saved=owned(root,path.join(root,'input-sources',relative));fs.mkdirSync(path.dirname(saved),{recursive:true});fs.writeFileSync(saved,bytes,{flag:'wx'});need(hashFile(original)===sha&&hashFile(saved)===sha,'RADIAL_SOURCE_CHANGED_DURING_CAPTURE:'+relative);return {path:relative,original,saved,sha256:sha};});}
export function verifySources(rows){const changes=[];for(const row of rows){if(hashFile(row.original)!==row.sha256||hashFile(row.saved)!==row.sha256)changes.push(row.path);}need(changes.length===0,'RADIAL_SOURCE_CHANGED:'+changes.join(','));return {status:'PASS',files:rows.map(({path,sha256})=>({path,sha256}))};}
export function compilerEnvironment(manifest){const result={...process.env,INCLUDE:manifest.includeDirs.join(';'),LIB:manifest.libDirs.join(';'),LIBPATH:'',PATH:path.dirname(manifest.executables.cl.path)+';'+process.env.SystemRoot+'\\System32;'+process.env.SystemRoot,VSLANG:'1033'};for(const name of ['CL','_CL_','LINK','_LINK_','NODE_OPTIONS','NODE_PATH'])delete result[name];return result;}
export function naturalTerminal(record,expected){return Boolean(record&&record.pid>0&&record.exitObserved===true&&record.closed===true&&Number.isInteger(record.naturalExit)&&record.naturalExit===expected&&record.signal===null&&!record.timedOut&&!record.launchError&&!record.killed&&record.outputComplete===true&&!record.outputTruncated);}
export async function executeStage({root,exe,args,label,env,timeoutMs}){
 need(Number.isInteger(timeoutMs)&&timeoutMs>=100&&timeoutMs<=90000,'RADIAL_STAGE_TIMEOUT_SCHEMA');
 const stdoutPath=owned(root,path.join(root,label+'.stdout.log')),stderrPath=owned(root,path.join(root,label+'.stderr.log'));
 const stdout=fs.openSync(stdoutPath,'wx'),stderr=fs.openSync(stderrPath,'wx');let child;
 const submitted={exe,args,cwd:root,windowsHide:true,startedAtUtc:new Date().toISOString(),timeoutMs,exeSha256:hashFile(exe),runner:{path:process.execPath,pid:process.pid,sha256:hashFile(process.execPath)}};
 const state={...submitted,pid:null,naturalExit:null,signal:null,timedOut:false,launchError:null,killed:false,exitObserved:false,closed:false};
 try{await new Promise(resolve=>{
  try{child=spawn(exe,args,{cwd:root,env,windowsHide:true,stdio:['ignore',stdout,stderr]});state.pid=child.pid??null;writeNew(path.join(root,label+'.submitted.json'),{...submitted,pid:state.pid});}
  catch(error){state.launchError=error.message;resolve();return;}
  const timer=setTimeout(()=>{state.timedOut=true;child.kill();},timeoutMs);
  child.once('error',error=>{state.launchError=error.message;});
  child.once('exit',(code,signal)=>{state.exitObserved=true;state.naturalExit=code;state.signal=signal;});
  child.once('close',(code,signal)=>{clearTimeout(timer);state.closed=true;if(!state.exitObserved){state.naturalExit=code;state.signal=signal;}state.killed=child.killed;resolve();});
 });}finally{fs.closeSync(stdout);fs.closeSync(stderr);}
 const stream=file=>({path:file,bytes:fs.statSync(file).size,sha256:hashFile(file)});state.stdout=stream(stdoutPath);state.stderr=stream(stderrPath);
 state.outputTruncated=state.stdout.bytes>16*1024*1024||state.stderr.bytes>16*1024*1024;
 state.outputComplete=state.closed&&state.exitObserved&&!state.timedOut&&!state.launchError&&!state.outputTruncated;
 state.endedAtUtc=new Date().toISOString();writeNew(path.join(root,label+'.terminal.json'),state);return state;
}
export function checkActualCompilerInputs({dependencyFile,compilerStdout,tools,ownedInputs}){
 const dependency=readJson(dependencyFile);need(typeof dependency.Data?.Source==='string'&&Array.isArray(dependency.Data?.Includes),'RADIAL_SOURCE_DEPENDENCIES_REQUIRED');
 const byTool=validateToolManifest(tools).byPath,byOwn=new Map(ownedInputs.map(f=>[key(f.path),f]));
 const check=(file,allowed)=>{const row=allowed.get(key(file));need(row,'RADIAL_UNDECLARED_COMPILER_INPUT:'+file);const actualSha256=hashFile(file);need(actualSha256===row.sha256,'RADIAL_COMPILER_INPUT_CHANGED:'+file);return {path:file,sha256:actualSha256};};
 const allowed=new Map([...byTool,...byOwn]);const includes=[dependency.Data.Source,...dependency.Data.Includes].map(file=>check(file,allowed));
 const libraryPaths=[...new Map([...compilerStdout.matchAll(/[A-Za-z]:\\[^\r\n]*?\.lib/gi)].map(m=>[key(m[0].trim()),m[0].trim()])).values()];
 need(libraryPaths.length>0,'RADIAL_ACTUAL_LINK_LIBRARIES_REQUIRED');const libraries=libraryPaths.map(file=>check(file,byTool));return {status:'PASS',includes,libraries};
}
export function inspectPeImports(file){
 const b=fs.readFileSync(file),pe=b.readUInt32LE(0x3c);need(b.toString('ascii',pe,pe+4)==='PE\0\0','RADIAL_PE_SIGNATURE');const count=b.readUInt16LE(pe+6),opt=pe+24,magic=b.readUInt16LE(opt);need(magic===0x20b,'RADIAL_X64_PE_REQUIRED');const directories=opt+112,sectionBase=opt+b.readUInt16LE(pe+20),sections=[];
 for(let i=0;i<count;i++){const p=sectionBase+i*40;sections.push({rva:b.readUInt32LE(p+12),size:Math.max(b.readUInt32LE(p+8),b.readUInt32LE(p+16)),raw:b.readUInt32LE(p+20)});}
 const offset=rva=>{const section=sections.find(s=>rva>=s.rva&&rva<s.rva+s.size);need(section,'RADIAL_PE_RVA');return section.raw+rva-section.rva;};
 const string=rva=>{const start=offset(rva),end=b.indexOf(0,start);need(end>=0,'RADIAL_PE_STRING');return b.toString('ascii',start,end);};
 const rva=b.readUInt32LE(directories+8),imports=[];need(rva>0,'RADIAL_PE_IMPORT_TABLE');let cursor=offset(rva);
 while(b.readUInt32LE(cursor)||b.readUInt32LE(cursor+12)){need(imports.length<32,'RADIAL_PE_IMPORT_BUDGET');imports.push(string(b.readUInt32LE(cursor+12)));cursor+=20;}
 need(imports.length>0&&imports.every(name=>name.toLowerCase()==='kernel32.dll'),'RADIAL_UNEXPECTED_GUI_OR_OTHER_IMPORT');return imports;
}
export function validateNativeResult({native,terminal,plan,variant,stdout,stderr}){
 const fault=variant==='original'?null:plan.sourceFaults.find(f=>f.id===variant);need(variant==='original'||fault,'RADIAL_UNDECLARED_VARIANT');
 const expectedExit=fault?fault.expectedNaturalExit:plan.positiveNaturalExit;
 need(naturalTerminal(terminal,expectedExit)&&native.naturalExit===expectedExit,'RADIAL_NATIVE_NOT_EXPECTED_NATURAL_EXIT');
 need(native.status===(fault?'ASSERTION_FAILED':'PASS')&&native.chain==='actual-extracted-native-post-create-consumer'&&native.postCreateSeedOnly===true&&native.actualCreateExecuted===false&&native.realOverlayOrWindowCreated===false,'RADIAL_NATIVE_RESULT_SCOPE_OR_STATUS');
 need(Array.isArray(native.cases)&&native.cases.length===(fault?1:12),'RADIAL_NATIVE_CASE_COUNT');
 const rows=native.cases.map((row,index)=>{
  need(row.id===(fault?fault.case:plan.positiveCases[index].id)&&row.status===(fault?'ASSERTION_FAILED':'PASS'),'RADIAL_NATIVE_CASE_OUTCOME');
  if(fault)need(row.assertion===fault.expectedAssertion,'RADIAL_NATIVE_WRONG_FAILURE_ASSERTION');
  const after=row.afterCleanup,keys=['queuedMessages','regions','windows','wireAllocations'];
  need(after&&JSON.stringify(Object.keys(after.resources??{}).sort())===JSON.stringify(keys)&&Object.values(after.resources).every(n=>n===0)&&after.environmentNull===true&&after.currentNull===true&&after.active===false,'RADIAL_NATIVE_CLEANUP_NOT_ZERO');
  need(Array.isArray(after.states)&&after.states.length>0&&after.states.every(s=>s.closed===true&&s.ownerNull===true&&s.window===0&&s.inputWindow===0&&['viewNull','controllerNull','compositionNull','deviceNull','targetNull','visualNull'].every(k=>s[k]===true)),'RADIAL_NATIVE_STATE_NOT_RETURNED');
  need(Array.isArray(row.trace)&&row.trace.length>0&&row.before&&row.beforeCleanup,'RADIAL_NATIVE_RAW_TRACE_REQUIRED');
  return {id:row.id,status:row.status,assertion:row.assertion??null,resources:after.resources};
 });
 const expectedLines=rows.map(r=>r.id+':'+r.status);need(stderr===''&&JSON.stringify(stdout.replace(/\r\n/g,'\n').split('\n').filter(Boolean))===JSON.stringify(expectedLines),'RADIAL_NATIVE_OUTPUT_INCOMPLETE_OR_UNEXPECTED');return rows;
}
export function requireCurrentOriginal(receipt,{runId,evidenceRoot}){
 need(receipt?.status==='PASS'&&receipt.variant==='original'&&receipt.runId===runId&&key(receipt.evidenceRoot)===key(evidenceRoot)&&inside(evidenceRoot,receipt.directory)&&receipt.cases?.length===12&&receipt.cases.every(c=>c.status==='PASS')&&receipt.nativeNaturalExit===0,'RADIAL_FAULT_REQUIRES_THIS_RUN_ORIGINAL12');return true;
}
