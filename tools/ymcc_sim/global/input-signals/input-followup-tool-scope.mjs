/** Identity only: original VM ports/consumer assertions stay in input-followup.mjs. */
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
import {sourcePath} from '../runner.mjs';
import {BUNDLED_AUTHORITY_SOURCES,lockBundledNodeTools,verifyBundledImplementation,verifyBundledToolScope,classifyBundledInput} from '../bundled-node-tool-authority.mjs';
import {getInputToolDeclaration,authorizeInputFile,parseInputLibraries,fileKey} from './inputs.mjs';
import {runObservedInputTool,sourceDependenciesResponseArgument,linkerResponseArguments} from './native-identity.mjs';
import {calibrationToolchainPreamble} from '../../../fixtures/hc_reviewed_calibration/toolchain.mjs';
const modulePath='tools/ymcc_sim/global/input-signals';
export const FOLLOWUP_IDENTITY_SOURCES=Object.freeze([...BUNDLED_AUTHORITY_SOURCES,
 'src/radial/testingState.ts','src/radial/availability.ts',
 ...['input-followup-tool-scope.mjs','input-followup-native-additions.json','inputs.mjs','native-identity.mjs','identity.mjs','tool-inputs.json'].map(name=>modulePath+'/'+name)]);
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const hashFile=file=>hash(fs.readFileSync(file));
const need=(ok,code)=>{if(!ok)throw Error('FOLLOWUP_'+code);};
const inside=(root,file)=>{const relative=path.relative(root,file);return relative!==''&&relative!=='..'&&!relative.startsWith('..'+path.sep)&&!path.isAbsolute(relative);};
const quote=value=>{need(!/["%\r\n]/.test(value),'UNSAFE_BUILD_PATH');return '"'+value+'"';};
function uniqueTools(rows){const map=new Map();for(const row of rows){const key=fileKey(row.path),previous=map.get(key);need(!previous||previous.sha256===row.sha256,'TOOL_IDENTITY_CONFLICT');map.set(key,{path:path.resolve(row.path),sha256:row.sha256,status:'OK'});}need(map.size<=3072,'ARTIFACT_BUDGET_EXCEEDED');return [...map.values()].sort((a,b)=>a.path<b.path?-1:1);}
function nativeDeclaration(repo){const original=getInputToolDeclaration(repo),extra=JSON.parse(fs.readFileSync(sourcePath(repo,modulePath+'/input-followup-native-additions.json'),'utf8'));need(extra.schemaVersion===1&&extra.kind==='input-followup-additional-installed-native-headers'&&Array.isArray(extra.files),'NATIVE_ADDITIONS_SCHEMA');const tool={...original,files:uniqueTools([...original.files,...extra.files])};for(const row of tool.files)need(hashFile(row.path)===row.sha256,'NATIVE_DECLARED_TOOL_CHANGED:'+row.path);return tool;}
export function followupToolDeclaration(repo){
 try{const authority=lockBundledNodeTools(repo),tool=nativeDeclaration(repo);for(const row of tool.files.filter(row=>row.path.split(/[\\/]/).includes('node_modules')))need(authority.tools.some(pin=>fileKey(pin.path)===fileKey(row.path)&&pin.sha256===row.sha256),'NATIVE_EXTRA_NODE_TOOL_NOT_BOUND');return {artifactInputs:uniqueTools([...authority.tools,...tool.files]).map(row=>row.path)};}
 catch(error){return {artifactInputs:[],blockedReason:'INPUT_FOLLOWUP_TOOL_AUTHORITY_UNAVAILABLE: '+error.message};}
}
export function beginFollowupScope(repoRoot,workDir,sourceInputs){
 const repo=fs.realpathSync(repoRoot);verifyBundledImplementation(repo);
 const directory=path.resolve(workDir);need(!fs.existsSync(directory),'FRESH_WORK_DIRECTORY_REQUIRED');
 fs.mkdirSync(directory,{recursive:true});need(fileKey(fs.realpathSync(directory))===fileKey(directory)&&!inside(repo,directory),'OWNED_WORK_DIRECTORY_REQUIRED');
 const authority=lockBundledNodeTools(repo),tool=nativeDeclaration(repo),tools=uniqueTools([...authority.tools,...tool.files]);
 const sources=sourceInputs.map(relative=>({path:relative,sha256:hashFile(sourcePath(repo,relative))}));
 const sourceMap=new Map(sources.map(row=>[fileKey(sourcePath(repo,row.path)),row]));
 const toolMap=new Map(tools.map(row=>[fileKey(fs.realpathSync(row.path)),row]));
 const scope={repoRoot:repo,directory,authority,tools,assets:new Map(),sources,observedSources:new Map(),observedTools:new Map(),bundles:[],native:null,phases:[],declaration:{repoRoot:repo,sources,sourceMap,tool,toolMap}};
 verifyFollowupScope(scope,'begin');return scope;
}
export function verifyFollowupScope(scope,phase){
 const proof=verifyBundledToolScope(scope.authority,scope.repoRoot);
 need(fileKey(fs.realpathSync(scope.directory))===fileKey(scope.directory),'OWNED_DIRECTORY_FORWARDED');
 for(const row of scope.tools)need(fileKey(fs.realpathSync(row.path))===fileKey(row.path)&&hashFile(row.path)===row.sha256,'TOOL_CHANGED:'+row.path);
 for(const row of scope.sources)need(hashFile(sourcePath(scope.repoRoot,row.path))===row.sha256,'SOURCE_CHANGED:'+row.path);
 for(const row of scope.assets.values())need(inside(scope.directory,row.path)&&fileKey(fs.realpathSync(row.path))===fileKey(row.path)&&hashFile(row.path)===row.sha256,'OWNED_OUTPUT_CHANGED:'+row.path);
 for(const row of scope.observedSources.values())need(hashFile(sourcePath(scope.repoRoot,row.path))===row.sha256,'LOADED_SOURCE_CHANGED:'+row.path);
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
 const tool=scope.declaration.tool,dumpbin=path.join(path.dirname(tool.compiler),'dumpbin.exe');
 for(const [file,role]of [[tool.compiler,'cl'],[tool.linker,'link'],[path.join(path.dirname(tool.compiler),'lib.exe'),'lib'],[dumpbin,'dumpbin'],[tool.vcvars,'vcvars'],[tool.where,'where']])authorizeInputFile(scope.declaration,file,'native-'+role);
 const located=await runObservedInputTool(scope,'native-vswhere',tool.vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],{timeout:10000});adoptObserved(scope,'native-vswhere');need(located.record.exitCode===0&&!located.record.error,'VSWHERE_FAILED');need(fileKey(path.join(located.stdout.toString('utf8').trim(),'VC/Auxiliary/Build/vcvars64.bat'))===fileKey(tool.vcvars),'VCVARS_NOT_DECLARED');
 const dir=path.join(scope.directory,'native');fs.mkdirSync(dir);
 for(const [name,text]of Object.entries(generated)){need(path.basename(name)===name,'GENERATED_INCLUDE_PATH');writeOutput(scope,path.join(dir,name),text,'exact source-extracted C++ include');}
 const source=path.join(dir,'fixture.cpp');writeOutput(scope,source,fs.readFileSync(fixture),'unchanged original native fixture');
 const executable=path.join(dir,'input-diagnostic-followup.exe'),deps=path.join(dir,'source-dependencies.json'),obj=path.join(dir,'fixture.obj');
 const rsp=path.join(dir,'compile.rsp'),cmd=path.join(dir,'compile.cmd'),paths=path.join(dir,'selected-tools.txt');
 const args=['/nologo','/std:c++17','/EHsc','/O1','/MT','/utf-8',`/I${quote(scope.repoRoot)}`,`/I${quote(dir)}`,quote(source),`/Fo:${quote(obj)}`,`/Fe:${quote(executable)}`,sourceDependenciesResponseArgument(deps),linkerResponseArguments()+' /INCREMENTAL:NO /NODEFAULTLIB:user32.lib'];
 writeOutput(scope,rsp,args.join('\r\n'),'actual unchanged compile options plus dependency/link trace');
 const commands=calibrationToolchainPreamble(tool.vcvars)+'set "CL="\r\nset "_CL_="\r\nset "LINK="\r\nset "_LINK_="\r\n'+['cl.exe','link.exe','lib.exe','dumpbin.exe'].map(name=>'where '+name+' >> selected-tools.txt\r\nif errorlevel 1 exit /b 1\r\n').join('')+'cl.exe @compile.rsp\r\nexit /b %errorlevel%\r\n';
 writeOutput(scope,cmd,commands,'reviewed calibration setup and exact native compile invocation');
 const env={...process.env};for(const name of ['CL','_CL_','LINK','_LINK_','INCLUDE','LIB','LIBPATH','VCToolsInstallDir','VCINSTALLDIR','WindowsSDKVersion','WindowsSdkDir'])delete env[name];env.VSCMD_SKIP_SENDTELEMETRY='1';
 const result=await runObservedInputTool(scope,'native-compile',tool.commandProcessor,['/d','/c','compile.cmd'],{cwd:dir,env});adoptObserved(scope,'native-compile');
 need(result.record.exitCode===0&&!result.record.error&&!result.record.signal&&!result.record.killed,'NATIVE_COMPILE_FAILED');
 bindFollowupOutput(scope,paths,'actual shell tool selections');const selected=fs.readFileSync(paths,'utf8').trim().split(/\r?\n/);
 const selections={};for(const name of ['cl.exe','link.exe','lib.exe','dumpbin.exe']){const actual=selected.find(file=>path.basename(file).toLowerCase()===name);need(actual&&fileKey(actual)===fileKey(path.join(path.dirname(tool.compiler),name)),'ACTUAL_NATIVE_TOOL_PATH_CHANGED:'+name);selections[name]=authorizeInputFile(scope.declaration,actual,'actual-command-selected-'+name);}
 const dependency=JSON.parse(fs.readFileSync(deps,'utf8').replace(/^\uFEFF/,''));need(fileKey(dependency?.Data?.Source??'')===fileKey(source)&&Array.isArray(dependency.Data.Includes)&&dependency.Data.Includes.length>0,'SOURCE_DEPENDENCIES_REQUIRED');
 const headers=[dependency.Data.Source,...dependency.Data.Includes].map(file=>authorizeInputFile(scope.declaration,file,'actual-native-header-or-source'));
 const libraries=parseInputLibraries(result.stdout,result.stderr).map(row=>({...row,...authorizeInputFile(scope.declaration,row.path,row.role)}));
 for(const file of [deps,obj,executable])bindFollowupOutput(scope,file,'actual nested native compilation output');
 const audit=await runObservedInputTool(scope,'native-dumpbin',dumpbin,['/imports',executable],{cwd:dir});adoptObserved(scope,'native-dumpbin');need(audit.record.exitCode===0&&!audit.record.error&&!audit.record.signal,'DUMPBIN_FAILED');
 const imports=Buffer.concat([audit.stdout,audit.stderr]).toString('utf8').toLowerCase();
 const forbidden=['user32.dll','win32u.dll','shell32.dll','ole32.dll','sendinput','keybd_event','getasynckeystate'].filter(token=>imports.includes(token));need(!forbidden.length,'FORBIDDEN_NATIVE_INPUT_IMPORT:'+forbidden.join(','));
 const importAudit={status:'PASS',forbiddenImports:forbidden,sha256:hash(audit.stdout),path:path.join(scope.directory,'native-dumpbin.stdout.log')};
 const identity={schemaVersion:1,scope:'Actual sourceDependencies and original ordered linker-library records; declared compiler support, not an OS loaded-module graph',selections,headers,libraries,dependency:{path:deps,sha256:hashFile(deps)},executable:{path:executable,sha256:hashFile(executable)},importAudit};
 const ledger=saveFollowupOutput(scope,'native-compiler-inputs.json',JSON.stringify(identity,null,2)+'\n','actual compiler header/library/output identity ledger');scope.native={...identity,ledger};
 verifyFollowupScope(scope,'native:after-compile');return {dir,executable,executableSha256:identity.executable.sha256,importAudit};
}
export function finishFollowupScope(scope){
 verifyFollowupScope(scope,'finish');
 return {sourceHashes:[...new Map([...scope.sources,...scope.observedSources.values(),...scope.assets.values()].map(row=>[row.path,{path:row.path,sha256:row.sha256}])).values()],identity:{schemaVersion:1,status:'PASS',authority:scope.authority,tools:scope.tools,bundles:scope.bundles,native:scope.native,phases:scope.phases,ownedOutputs:[...scope.assets.values()],osLoadedModuleGraphObserved:false,actualOsInput:false}};
}
