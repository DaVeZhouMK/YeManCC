/** Identity only: original VM ports/consumer assertions stay in input-followup.mjs. */
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
import {sourcePath} from '../runner.mjs';
import {BUNDLED_AUTHORITY_SOURCES,lockBundledNodeTools,verifyBundledImplementation,verifyBundledToolScope,classifyBundledInput} from '../bundled-node-tool-authority.mjs';
import {authorizeInputFile,parseInputLibraries,fileKey} from './inputs.mjs';
import {runObservedInputTool,sourceDependenciesResponseArgument,linkerResponseArguments} from './native-identity.mjs';
import {startupToolchainEnvironment,verifyStartupToolchain} from '../controller-startup-return/toolchain.mjs';
const modulePath='tools/ymcc_sim/global/input-signals';
export const FOLLOWUP_IDENTITY_SOURCES=Object.freeze([...BUNDLED_AUTHORITY_SOURCES,
 'src/radial/testingState.ts','src/radial/availability.ts','tools/ymcc_sim/global/controller-startup-return/toolchain.mjs',
 ...['input-followup-tool-scope.mjs','input-followup-native-additions.json','inputs.mjs','native-identity.mjs','identity.mjs'].map(name=>modulePath+'/'+name)]);
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const hashFile=file=>hash(fs.readFileSync(file));
const need=(ok,code)=>{if(!ok)throw Error('FOLLOWUP_'+code);};
const inside=(root,file)=>{const relative=path.relative(root,file);return relative!==''&&relative!=='..'&&!relative.startsWith('..'+path.sep)&&!path.isAbsolute(relative);};
const quote=value=>{need(!/["%\r\n]/.test(value),'UNSAFE_BUILD_PATH');return '"'+value+'"';};
function uniqueTools(rows){const map=new Map();for(const row of rows){const key=fileKey(row.path),previous=map.get(key);need(!previous||previous.sha256===row.sha256,'TOOL_IDENTITY_CONFLICT');map.set(key,{path:path.resolve(row.path),sha256:row.sha256,status:'OK'});}need(map.size<=3072,'ARTIFACT_BUDGET_EXCEEDED');return [...map.values()].sort((a,b)=>a.path<b.path?-1:1);}
function nativeDeclaration(repo){
 const tool=JSON.parse(fs.readFileSync(sourcePath(repo,modulePath+'/input-followup-native-additions.json'),'utf8'));
 need(tool.schemaVersion===2&&tool.kind==='fixed-startup-return-x64-v1'&&tool.platform===process.platform&&process.platform==='win32'&&process.arch==='x64','NATIVE_FIXED_TOOLCHAIN_SCHEMA');
 const bin=path.join(tool.msvcRoot,'bin/Hostx64/x64');
 const expectedDirs=[bin,path.join(bin,'1033')].map(fileKey),rows=tool.directories??[];
 need(rows.length===expectedDirs.length&&rows.every((row,index)=>fileKey(row.path)===expectedDirs[index]),'NATIVE_COMPILER_DIRECTORIES_CHANGED');
 need(Array.isArray(tool.files)&&tool.files.every(row=>inside(tool.msvcRoot,row.path)||inside(tool.sdkRoot,row.path)),'NATIVE_TOOL_OUTSIDE_FIXED_ROOTS');
 const pins=new Set(tool.files.map(row=>fileKey(row.path)));
 for(const name of ['cl.exe','link.exe','dumpbin.exe','lib.exe','cl.exe.config','link.exe.config'])need(pins.has(fileKey(path.join(bin,name))),'NATIVE_FIXED_TOOL_MISSING:'+name);
 verifyStartupToolchain(tool);
 return {...tool,compiler:path.join(bin,'cl.exe'),linker:path.join(bin,'link.exe'),dumpbin:path.join(bin,'dumpbin.exe'),librarian:path.join(bin,'lib.exe')};
}
export function followupToolDeclaration(repo){
 try{const tool=nativeDeclaration(repo),authority=lockBundledNodeTools(repo);return {artifactInputs:uniqueTools([...authority.tools,...tool.files]).map(row=>row.path)};}
 catch(error){return {artifactInputs:[],blockedReason:'INPUT_FOLLOWUP_TOOL_AUTHORITY_UNAVAILABLE: '+error.message};}
}
function requireDirectDirectory(directory){
 const stat=fs.lstatSync(directory);
 need(stat.isDirectory()&&!stat.isSymbolicLink()&&fileKey(fs.realpathSync(directory))===fileKey(directory),'OWNED_WORK_DIRECTORY_REQUIRED');
}
function checkFreshFollowupDirectory(repo,directory){
 need(fileKey(directory)!==fileKey(repo)&&!inside(repo,directory),'OWNED_WORK_DIRECTORY_REQUIRED');
 for(let cursor=directory;;cursor=path.dirname(cursor)){
  try{
   fs.lstatSync(cursor);
  }catch(error){if(error.code==='ENOENT'&&path.dirname(cursor)!==cursor)continue;throw error;}
  need(cursor!==directory,'FRESH_WORK_DIRECTORY_REQUIRED');
  requireDirectDirectory(cursor);
  return;
 }
}
export function beginFollowupScope(repoRoot,workDir,sourceInputs){
 const repo=fs.realpathSync(repoRoot),directory=path.resolve(workDir);
 checkFreshFollowupDirectory(repo,directory);
 verifyBundledImplementation(repo);
 const tool=nativeDeclaration(repo),authority=lockBundledNodeTools(repo),tools=uniqueTools([...authority.tools,...tool.files]);
 const sources=sourceInputs.map(relative=>({path:relative,sha256:hashFile(sourcePath(repo,relative))}));
 const sourceMap=new Map(sources.map(row=>[fileKey(sourcePath(repo,row.path)),row]));
 const toolMap=new Map(tools.map(row=>[fileKey(fs.realpathSync(row.path)),row]));
 const parent=path.dirname(directory);fs.mkdirSync(parent,{recursive:true});requireDirectDirectory(parent);
 fs.mkdirSync(directory);requireDirectDirectory(directory);
 const scope={repoRoot:repo,directory,authority,tools,assets:new Map(),sources,observedSources:new Map(),observedTools:new Map(),bundles:[],native:null,phases:[],declaration:{repoRoot:repo,sources,sourceMap,tool,toolMap}};
 verifyFollowupScope(scope,'begin');return scope;
}
export function verifyFollowupScope(scope,phase){
 const proof=verifyBundledToolScope(scope.authority,scope.repoRoot);
 need(fileKey(fs.realpathSync(scope.directory))===fileKey(scope.directory),'OWNED_DIRECTORY_FORWARDED');
 // Only this checkpoint's fresh tool proof and source reads may be reused.
 const nodeDigests=new Map(proof.tools.map(row=>[fileKey(row.path),row.sha256]));
 for(const row of scope.tools){
  const key=fileKey(row.path);
  need(fileKey(fs.realpathSync(row.path))===key&&(nodeDigests.get(key)??hashFile(row.path))===row.sha256,'TOOL_CHANGED:'+row.path);
 }
 const sourceDigests=new Map();
 const checkSource=(row,code)=>{
  const file=sourcePath(scope.repoRoot,row.path);
  if(!sourceDigests.has(file))sourceDigests.set(file,hashFile(file));
  need(sourceDigests.get(file)===row.sha256,code+row.path);
 };
 for(const row of scope.sources)checkSource(row,'SOURCE_CHANGED:');
 for(const row of scope.assets.values())need(inside(scope.directory,row.path)&&fileKey(fs.realpathSync(row.path))===fileKey(row.path)&&hashFile(row.path)===row.sha256,'OWNED_OUTPUT_CHANGED:'+row.path);
 for(const row of scope.observedSources.values())checkSource(row,'LOADED_SOURCE_CHANGED:');
 const record={phase,packageCount:proof.packageCount,fileCount:proof.fileCount,graphSha256:proof.graphSha256,declaredToolCount:scope.tools.length,ownedOutputs:scope.assets.size,verified:true};scope.phases.push(record);return record;
}
export function bindFollowupOutput(scope,file,role){
 const absolute=path.resolve(file);need(inside(scope.directory,absolute)&&fileKey(fs.realpathSync(absolute))===fileKey(absolute),'OUTPUT_OUTSIDE_EXACT_OWNED_WORK');
 const digest=hashFile(absolute),previous=scope.assets.get(fileKey(absolute));need(!previous||previous.sha256===digest,'OWNED_OUTPUT_CHANGED:'+absolute);
 const row={path:absolute,sha256:digest,role};scope.assets.set(fileKey(absolute),row);scope.declaration.sourceMap.set(fileKey(absolute),row);return row;
}
function writeOutput(scope,file,bytes,role){fs.writeFileSync(file,bytes,{flag:'wx'});return bindFollowupOutput(scope,file,role);}
export function saveFollowupOutput(scope,name,bytes,role){return writeOutput(scope,path.join(scope.directory,name),bytes,role);}
export async function auditedFollowupBundle(scope,source,entryFile){
 const label=entryFile?'legacy-selftest':'keyboard-model';verifyFollowupScope(scope,label+':before-compile');
 const require=createRequire(path.join(scope.repoRoot,'package.json'));
 need(fileKey(fs.realpathSync(require.resolve('esbuild')))===fileKey(scope.authority.compilerEntry),'COMPILER_NOT_BOUND');
 const esbuild=require(scope.authority.compilerEntry),loadedSources=new Map(),loadedTools=new Map();
 const options={...(source?{stdin:{contents:source,resolveDir:scope.repoRoot,sourcefile:'input-followup-entry.ts',loader:'ts'}}:{entryPoints:[path.join(scope.repoRoot,entryFile)]}),absWorkingDir:scope.repoRoot,bundle:true,write:false,platform:'node',format:'cjs',metafile:true,logLevel:'silent'};
 const observedPlugin={name:'followup-declared-byte-inputs',setup(build){build.onLoad({filter:/\.(?:[cm]?js|jsx|tsx?|json)$/,namespace:'file'},({path:file})=>{
  const bytes=fs.readFileSync(file),classification=classifyBundledInput(scope.repoRoot,file,bytes,scope.authority),map=classification.role==='tool'?loadedTools:loadedSources;
  if(classification.role==='source')need(scope.sources.some(row=>row.path===classification.row.path&&row.sha256===classification.row.sha256),'PRODUCT_SOURCE_NOT_DECLARED:'+classification.row.path);
  map.set(fileKey(fs.realpathSync(file)),classification.row);const extension=path.extname(file).slice(1);return {contents:bytes.toString('utf8'),resolveDir:path.dirname(file),loader:['ts','tsx','jsx','json'].includes(extension)?extension:'js'};
 });}};
 const result=await esbuild.build({...options,plugins:[observedPlugin]});need(result.outputFiles.length===1,'ONE_BUNDLE_REQUIRED');
 const observed=new Set();let virtualInputs=0;
 for(const [input,meta]of Object.entries(result.metafile.inputs)){
  if(source&&input==='input-followup-entry.ts'){need(meta.bytes===Buffer.byteLength(source),'STDIN_BYTES_CHANGED');virtualInputs++;continue;}
  const file=path.resolve(scope.repoRoot,input),bytes=fs.readFileSync(file),classification=classifyBundledInput(scope.repoRoot,file,bytes,scope.authority),key=fileKey(fs.realpathSync(file));
  need((classification.role==='tool'?loadedTools:loadedSources).get(key)?.sha256===classification.row.sha256&&meta.bytes===bytes.length&&!observed.has(key),'METAFILE_INPUT_CHANGED:'+input);observed.add(key);
 }
 need(virtualInputs===(source?1:0)&&observed.size===loadedSources.size+loadedTools.size,'METAFILE_MEMBERSHIP_CHANGED');
 for(const [key,row]of loadedSources)scope.observedSources.set(key,row);for(const [key,row]of loadedTools)scope.observedTools.set(key,row);
 const bundle=saveFollowupOutput(scope,label+'.bundle.cjs',result.outputFiles[0].contents,'actual nested compiler bundle');
 const meta=saveFollowupOutput(scope,label+'.metafile.json',JSON.stringify(result.metafile,null,2)+'\n','actual nested compiler metafile');
 if(source)saveFollowupOutput(scope,label+'.stdin.ts',source,'actual unchanged stdin compiler source');
 const code=result.outputFiles[0].text;need(hash(Buffer.from(code))===bundle.sha256,'VM_CODE_NOT_EXACT_BUNDLE_BYTES');
 const vmCode=saveFollowupOutput(scope,label+'.vm-code.cjs',code,'exact string bytes supplied to vm.Script');
 const record={label,bundle,metafile:meta,vmCode,vmCodeSha256:hash(Buffer.from(code)),productInputs:[...loadedSources.values()],toolInputs:[...loadedTools.values()]};scope.bundles.push(record);
 verifyFollowupScope(scope,label+':after-compile');return {code,record,inputs:record.productInputs};
}
export function verifyFollowupVmCode(scope,built,phase){need(hash(Buffer.from(built.code))===built.record.vmCodeSha256,'VM_CODE_CHANGED');return verifyFollowupScope(scope,phase);}
function adoptObserved(scope,name){for(const suffix of ['.stdout.log','.stderr.log','.process.json'])bindFollowupOutput(scope,path.join(scope.directory,name+suffix),'original observed native tool ledger');}
export async function buildAuditedFollowupNative(scope,{fixture,generated}){
 verifyFollowupScope(scope,'native:before-compile');
 const tool=scope.declaration.tool;
 const selections={};
 for(const [name,file,role]of [['cl.exe',tool.compiler,'exact-compiler-command'],['link.exe',tool.linker,'fixed-compiler-linker'],['lib.exe',tool.librarian,'pinned-librarian-not-executed'],['dumpbin.exe',tool.dumpbin,'exact-import-audit-command']])selections[name]=authorizeInputFile(scope.declaration,file,role);
 const dir=path.join(scope.directory,'native');fs.mkdirSync(dir);
 for(const [name,text]of Object.entries(generated)){need(path.basename(name)===name,'GENERATED_INCLUDE_PATH');writeOutput(scope,path.join(dir,name),text,'exact source-extracted C++ include');}
 const source=path.join(dir,'fixture.cpp');writeOutput(scope,source,fs.readFileSync(fixture),'unchanged original native fixture');
 const executable=path.join(dir,'input-diagnostic-followup.exe'),deps=path.join(dir,'source-dependencies.json'),obj=path.join(dir,'fixture.obj');
 const rsp=path.join(dir,'compile.rsp');
 const args=['/nologo','/std:c++20','/EHsc','/O1','/MT','/utf-8',`/I${quote(scope.repoRoot)}`,`/I${quote(dir)}`,quote(source),`/Fo:${quote(obj)}`,`/Fe:${quote(executable)}`,sourceDependenciesResponseArgument(deps),linkerResponseArguments()+' /INCREMENTAL:NO /NODEFAULTLIB:user32.lib'];
 writeOutput(scope,rsp,args.join('\r\n'),'actual unchanged compile options plus dependency/link trace');
 const env={...startupToolchainEnvironment(tool),TEMP:dir,TMP:dir};
 const environment=saveFollowupOutput(scope,'native-environment.json',JSON.stringify(env,null,2)+'\n','fixed native child environment');
 const result=await runObservedInputTool(scope,'native-compile',tool.compiler,['@compile.rsp'],{cwd:dir,env});adoptObserved(scope,'native-compile');
 need(result.record.exitCode===0&&!result.record.error&&!result.record.signal&&!result.record.killed,'NATIVE_COMPILE_FAILED');
 const dependency=JSON.parse(fs.readFileSync(deps,'utf8').replace(/^\uFEFF/,''));need(fileKey(dependency?.Data?.Source??'')===fileKey(source)&&Array.isArray(dependency.Data.Includes)&&dependency.Data.Includes.length>0,'SOURCE_DEPENDENCIES_REQUIRED');
 const headers=[dependency.Data.Source,...dependency.Data.Includes].map(file=>authorizeInputFile(scope.declaration,file,'actual-native-header-or-source'));
 const libraries=parseInputLibraries(result.stdout,result.stderr).map(row=>({...row,...authorizeInputFile(scope.declaration,row.path,row.role)}));
 for(const file of [deps,obj,executable])bindFollowupOutput(scope,file,'actual nested native compilation output');
 const audit=await runObservedInputTool(scope,'native-dumpbin',tool.dumpbin,['/imports',executable],{cwd:dir,env});adoptObserved(scope,'native-dumpbin');need(audit.record.exitCode===0&&!audit.record.error&&!audit.record.signal,'DUMPBIN_FAILED');
 const imports=Buffer.concat([audit.stdout,audit.stderr]).toString('utf8').toLowerCase();
 const forbidden=['user32.dll','win32u.dll','shell32.dll','ole32.dll','sendinput','keybd_event','getasynckeystate'].filter(token=>imports.includes(token));need(!forbidden.length,'FORBIDDEN_NATIVE_INPUT_IMPORT:'+forbidden.join(','));
 const importAudit={status:'PASS',forbiddenImports:forbidden,sha256:hash(audit.stdout),path:path.join(scope.directory,'native-dumpbin.stdout.log')};
 const identity={schemaVersion:1,scope:'Actual sourceDependencies and original ordered linker-library records; declared compiler support, not an OS loaded-module graph',selections,environment,headers,libraries,dependency:{path:deps,sha256:hashFile(deps)},executable:{path:executable,sha256:hashFile(executable)},importAudit};
 const ledger=saveFollowupOutput(scope,'native-compiler-inputs.json',JSON.stringify(identity,null,2)+'\n','actual compiler header/library/output identity ledger');scope.native={...identity,ledger};
 verifyFollowupScope(scope,'native:after-compile');return {dir,executable,executableSha256:identity.executable.sha256,importAudit};
}
export function finishFollowupScope(scope){
 verifyFollowupScope(scope,'finish');
 return {sourceHashes:[...new Map([...scope.sources,...scope.observedSources.values(),...scope.assets.values()].map(row=>[row.path,{path:row.path,sha256:row.sha256}])).values()],identity:{schemaVersion:1,status:'PASS',authority:scope.authority,tools:scope.tools,bundles:scope.bundles,native:scope.native,phases:scope.phases,ownedOutputs:[...scope.assets.values()],osLoadedModuleGraphObserved:false,actualOsInput:false}};
}
