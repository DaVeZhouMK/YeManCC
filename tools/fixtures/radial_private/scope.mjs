import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {createRequire} from 'node:module';
import {BUNDLED_AUTHORITY_SOURCES,lockBundledNodeTools,classifyBundledInput,verifyBundledToolScope,verifyBundledImplementation} from '../../ymcc_sim/global/bundled-node-tool-authority.mjs';
export const run=promisify(execFile);
export const LIMIT=3072;
export const SOURCE_MEMBERS=[...BUNDLED_AUTHORITY_SOURCES,'tools/fixtures/radial_private/scope.mjs','tools/fixtures/radial_private/native-environment.h','tools/fixtures/radial_private/tool-versions.ps1','tools/fixtures/radial_private/native-tools.json'];
export const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const key=file=>path.resolve(file).toLowerCase();
export const inside=(root,file)=>{const r=path.relative(path.resolve(root),path.resolve(file));return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
export function need(ok,message){if(!ok)throw Error('RADIAL_PRIVATE_'+message);}
export function readJson(file){return JSON.parse(fs.readFileSync(file,'utf8').replace(/^\ufeff/,''));}
export function save(file,value){fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');}
export function unique(rows){const found=new Map();for(const row of rows){const prev=found.get(key(row.path));need(!prev||prev.sha256===row.sha256,'IDENTITY_CONFLICT');found.set(key(row.path),{path:path.resolve(row.path),sha256:row.sha256,status:'OK'});}return [...found.values()].sort((a,b)=>a.path.localeCompare(b.path));}
export function nativeTools(repo){
 const manifestFile=path.join(repo,'tools/fixtures/radial_private/native-tools.json'),manifest=readJson(manifestFile);
 need(manifest.schemaVersion===1&&manifest.maxArtifactInputs===LIMIT&&manifest.kind==='radial-explicit-native-toolchain','TOOL_MANIFEST_SCHEMA');
 const files=manifest.files.map(row=>{const file=row.repoRelative?path.resolve(repo,row.repoRelative):row.path;need(path.isAbsolute(file)&&/^[a-f0-9]{64}$/.test(row.sha256),'TOOL_ROW');need(!row.repoRelative||inside(repo,file),'TOOL_REPO_ESCAPE');need(fs.realpathSync(file).toLowerCase()===key(file),'TOOL_REPARSE');need(sha(file)===row.sha256,'TOOL_CHANGED:'+file);return {...row,path:file,status:'OK'};});
 const pins=new Map(files.map(row=>[key(row.path),row]));
 for(const directory of manifest.directories){need(directory.membership==='complete-recursive-files-no-reparse','TOOL_DIRECTORY_POLICY');const entries=fs.readdirSync(directory.path,{recursive:true,withFileTypes:true});need(entries.every(e=>!e.isSymbolicLink()),'TOOL_DIRECTORY_REPARSE');const members=entries.filter(e=>e.isFile()).map(e=>path.join(e.parentPath||e.path,e.name));const declared=files.filter(row=>inside(directory.path,row.path));need(members.length===declared.length&&members.every(file=>pins.has(key(file))),'TOOL_DIRECTORY_MEMBERSHIP');}need(pins.size===files.length&&files.length<=LIMIT,'TOOL_LIMIT_OR_DUPLICATE');
 for(const name of ['compiler','linker','powershell','cmd','staticLoader','webviewHeader','webviewOptionsHeader']){const p=manifest.selected[name];const file=name==='staticLoader'||name.startsWith('webview')?path.resolve(repo,p):p;need(pins.has(key(file)),'SELECTED_TOOL_UNDECLARED:'+name);}
 return {manifestFile,manifest,files,pins};
}
function reviewedRuntime(){
 const file=process.env.YMCC_RADIAL_EXECUTION_REVIEW,pin=process.env.YMCC_RADIAL_EXECUTION_REVIEW_SHA256;if(!file&&!pin)return [];
 need(file&&pin&&sha(file)===pin,'REVIEW_DECLARATION_PIN');const approval=readJson(file);need(approval.approved===true&&Array.isArray(approval.runtimeFiles),'REVIEW_DECLARATION_SCHEMA');
 const rows=[{path:path.resolve(file),sha256:pin},...approval.runtimeFiles.map(row=>({path:path.resolve(approval.runtimeRoot,row.path),sha256:row.sha256}))];
 for(const row of rows)need(inside(approval.runtimeRoot,row.path)||key(row.path)===key(file),'REVIEW_RUNTIME_ESCAPE');for(const row of rows)need(sha(row.path)===row.sha256,'REVIEW_RUNTIME_CHANGED');return rows;
}
export function toolDeclaration(repo){
 try{const node=lockBundledNodeTools(repo),native=nativeTools(repo);const rows=unique([...node.tools,...native.files,...reviewedRuntime()]);need(rows.length<=LIMIT,'COMBINED_ARTIFACT_LIMIT');return {artifactInputs:rows.map(row=>row.path)};}
 catch(error){return {artifactInputs:[],blockedReason:'RADIAL_PRIVATE_TOOL_AUTHORITY_UNAVAILABLE: '+error.message};}
}
export function begin(repo,out,entry,{resume=false}={}){
 const root=fs.realpathSync(repo),target=path.resolve(out);verifyBundledImplementation(root);need(target!==root&&!inside(target,root),'OWNED_OUT_REQUIRED');
 if(!resume){need(!fs.existsSync(target),'FRESH_OUT_REQUIRED');fs.mkdirSync(target,{recursive:true});}else need(fs.statSync(target).isDirectory(),'PREPARED_OUT_REQUIRED');
 const node=lockBundledNodeTools(root),native=nativeTools(root),sourceHashes={};
 for(const relative of [...SOURCE_MEMBERS,'tsconfig.json',entry])sourceHashes[relative]=sha(path.join(root,relative));
 const artifacts=unique([...node.tools,...native.files,...reviewedRuntime()]);need(artifacts.length<=LIMIT,'COMBINED_ARTIFACT_LIMIT');
 return {repo:root,out:target,node,native,sourceHashes,observed:new Map(),generated:[],entry,toolArtifacts:artifacts};
}
export function source(scope,file,contents=fs.readFileSync(file)){
 const row=classifyBundledInput(scope.repo,file,contents,scope.node);scope.observed.set(key(fs.realpathSync(file)),row);
 if(row.role==='source')scope.sourceHashes[row.row.path]=row.row.sha256;
 return row;
}
export function inputPlugin(scope){return {name:'radial-complete-input-authority',setup(build){
 build.onLoad({filter:/\.(?:[cm]?js|jsx|tsx?|json|css|svg|png|jpg|webp|woff2)$/,namespace:'file'},args=>{source(scope,args.path);return undefined;});
}};}
export function bindBuild(scope,result,label){
 need(result.metafile&&result.outputFiles,'ACTUAL_BUILD_GRAPH_REQUIRED');
 for(const input of Object.keys(result.metafile.inputs)){
  if(input==='<stdin>')continue;
  const file=path.resolve(scope.repo,input);source(scope,file);
  need(scope.observed.has(key(fs.realpathSync(file))),'UNOBSERVED_BUILD_INPUT:'+input);
 }
 const file=path.join(scope.out,label+'.metafile.json');save(file,result.metafile);generated(scope,file,'actual esbuild metafile');
}
export function generated(scope,file,role){
 need(inside(scope.out,file)&&inside(fs.realpathSync(scope.out),fs.realpathSync(file)),'GENERATED_OUTPUT_ESCAPE');
 const row={path:path.resolve(file),sha256:sha(file),status:'OK',role};scope.generated.push(row);scope.sourceHashes[row.path]=row.sha256;return row;
}
export function verify(scope){
 verifyBundledToolScope(scope.node,scope.repo);nativeTools(scope.repo);reviewedRuntime();
 for(const [relative,digest]of Object.entries(scope.sourceHashes))need(sha(path.isAbsolute(relative)?relative:path.join(scope.repo,relative))===digest,'SOURCE_OR_GENERATED_CHANGED:'+relative);
 for(const row of scope.toolArtifacts)need(sha(row.path)===row.sha256,'TOOL_CHANGED_AFTER:'+row.path);
 need(unique([...scope.toolArtifacts,...scope.generated]).length<=LIMIT,'FINAL_ARTIFACT_LIMIT');
 return {nodeGraphSha256:scope.node.graphSha256,nodePackageCount:scope.node.packageCount,nodeFileCount:scope.node.fileCount,explicitNativeCount:scope.native.files.length,artifactCount:unique([...scope.toolArtifacts,...scope.generated]).length,maxArtifactInputs:LIMIT,osLoadedModuleGraph:false};
}
export function receipt(scope,extra={}){
 return {schemaVersion:1,sourceHashes:scope.sourceHashes,nodeToolAuthority:scope.node,toolArtifacts:scope.toolArtifacts,toolDependencyInputs:[...scope.observed.values()].filter(r=>r.role==='tool').map(r=>r.row),compiledArtifacts:scope.generated,...extra};
}
export async function fixture(scope,name){
 const req=createRequire(path.join(scope.repo,'package.json')),{build}=req('esbuild');
 const result=await build({entryPoints:[path.join(scope.repo,name)],absWorkingDir:scope.repo,bundle:true,platform:'node',format:'esm',write:false,metafile:true,logLevel:'silent',plugins:[inputPlugin(scope)]});
 const bundle=path.join(scope.out,'fixture.mjs');need(result.outputFiles.length===1,'FRAME_BUNDLE_COUNT');fs.writeFileSync(bundle,result.outputFiles[0].contents);generated(scope,bundle,'actual executed Node frame bundle');bindBuild(scope,result,'fixture');
 const resultRun=await run(process.execPath,[bundle,path.join(scope.out,'frame.json')],{cwd:scope.repo,windowsHide:true,timeout:10000,maxBuffer:2e6});fs.writeFileSync(path.join(scope.out,'fixture.log'),resultRun.stdout+resultRun.stderr);generated(scope,path.join(scope.out,'frame.json'),'actual production-model frame');
}
function replaceOne(text,old,next){need(text.split(old).length===2,'TRANSFORM_ANCHOR:'+old.slice(0,70));return text.replace(old,next);}
export function instrument(scope,monitor){
 const read=relative=>{const file=path.join(scope.repo,relative);source(scope,file);return fs.readFileSync(file,'utf8');};
 const transforms=[];
 function emit(name,original,text,restore){need(restore(text)===original,'NON_REVERSIBLE:'+name);const file=path.join(scope.out,name);fs.writeFileSync(file,text);generated(scope,file,'reversible test-only native source');transforms.push({path:file,originalSha256:crypto.createHash('sha256').update(original).digest('hex'),sha256:sha(file),reversible:true});}
 const screen=read('native/screen_touchpad_input_surface.h');
 const screenNew=replaceOne(replaceOne(screen,'HDC screen=GetDC(nullptr);','HDC screen=GetDC(root_);'),'ReleaseDC(nullptr,screen);','ReleaseDC(root_,screen);');
 emit('screen-under-test.h',screen,screenNew,s=>s.replace('HDC screen=GetDC(root_);','HDC screen=GetDC(nullptr);').replace('ReleaseDC(root_,screen);','ReleaseDC(nullptr,screen);'));
 const originalMonitor=read('native/monitor_desktop_hud.h');let carrier=replaceOne(originalMonitor,'#include "screen_touchpad_input_surface.h"','#include "screen-under-test.h"');
 const changes=[['GetForegroundWindow()','ymcc::radialfixture::foreground()'],['MonitorFromWindow(','ymcc::radialfixture::monitorFromWindow('],['GetMonitorInfoW(','ymcc::radialfixture::monitorInfo('],['OpenProcess(','ymcc::radialfixture::denyForeignProcess('],['ProcessIdToSessionId(','ymcc::radialfixture::memorySession('],['EnumWindows(','ymcc::radialfixture::ownedWindows(']];
 for(const [a,b]of changes)carrier=carrier.replaceAll(a,b);
 carrier=replaceOne(carrier,'observed,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS','observed,GetCurrentProcessId(),GetCurrentThreadId(),WINEVENT_OUTOFCONTEXT');
 const friend='\n friend struct MonitorJoinTestAccess; // isolated test access only\n';if(monitor)carrier=replaceOne(carrier,'class Carrier {','class Carrier {'+friend);
 emit('monitor-under-test.h',originalMonitor,carrier,s=>{if(monitor)s=s.replace('class Carrier {'+friend,'class Carrier {');s=s.replace('observed,GetCurrentProcessId(),GetCurrentThreadId(),WINEVENT_OUTOFCONTEXT','observed,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS');for(const [a,b]of changes)s=s.replaceAll(b,a);return s.replace('#include "screen-under-test.h"','#include "screen_touchpad_input_surface.h"');});
 const overlay=read('native/radial_overlay.h');let shadow=replaceOne(overlay,'#include "monitor_desktop_hud.h"','#include "monitor-under-test.h"');
 const overlayChanges=[['MonitorFromPoint(','ymcc::radialfixture::monitorFromPoint('],['MonitorFromWindow(','ymcc::radialfixture::monitorFromWindow('],['GetMonitorInfoW(','ymcc::radialfixture::monitorInfo(']];for(const [a,b]of overlayChanges)shadow=shadow.replaceAll(a,b);
 emit('overlay-under-test.h',overlay,shadow,s=>{for(const [a,b]of overlayChanges)s=s.replaceAll(b,a);return s.replace('#include "monitor-under-test.h"','#include "monitor_desktop_hud.h"');});
 const original=read('tools/radial_native_selftest.cpp');let helpers=replaceOne(original,'#include "../native/radial_overlay.h"','#include "native-environment.h"\n#include "overlay-under-test.h"');
 const edits=[
 [' const auto before=GetForegroundWindow();POINT cursorBefore{};GetCursorPos(&cursorBefore);',' // Test-only environment observations: no user cursor, foreground or station reads.'],
 ['  wchar_t station[256]{};DWORD bytes=0;ensure(GetUserObjectInformationW(GetProcessWindowStation(),UOI_NAME,station,sizeof(station),&bytes)!=FALSE,"Window station name unavailable");','  // lpDesktop names only the desktop created below; no station inspection.'],
 ['std::wstring desktop=std::wstring(station)+L"\\\\"+name;','std::wstring desktop=name;'],
 [' POINT cursorAfter{};GetCursorPos(&cursorAfter);launch["userForegroundUnchanged"]=GetForegroundWindow()==before;launch["userCursorUnchanged"]=cursorBefore.x==cursorAfter.x&&cursorBefore.y==cursorAfter.y;',' launch["environmentObservation"]="test-only-memory";launch["userCursorObserved"]=false;launch["userForegroundObserved"]=false;launch["stationObserved"]=false;'],
 ['CreateCoreWebView2EnvironmentWithOptions(nullptr,profile.c_str(),nullptr,','ymcc::radialfixture::createEnvironment(profile.c_str(),'],
 ['GetMonitorInfoW(','ymcc::radialfixture::monitorInfo('],['MonitorFromPoint(','ymcc::radialfixture::monitorFromPoint(']
 ];
 for(const [a,b]of edits)helpers=a.includes('(')&&['GetMonitorInfoW(','MonitorFromPoint('].includes(a)?helpers.replaceAll(a,b):replaceOne(helpers,a,b);
 emit('radial-test-helpers.cpp',original,helpers,s=>{for(const [a,b]of [...edits].reverse())s=s.replaceAll(b,a);return s.replace('#include "native-environment.h"\n#include "overlay-under-test.h"','#include "../native/radial_overlay.h"');});
 fs.copyFileSync(path.join(scope.repo,'tools/fixtures/radial_private/native-environment.h'),path.join(scope.out,'native-environment.h'));generated(scope,path.join(scope.out,'native-environment.h'),'test-only native terminals');
 let entry=path.join(scope.out,'radial-test-helpers.cpp');
 if(monitor){const original=read('tools/radial_monitor_selftest.cpp');const text=replaceOne(original,'CreateCoreWebView2EnvironmentWithOptions(nullptr,profile.c_str(),nullptr,','ymcc::radialfixture::createEnvironment(profile.c_str(),');emit('monitor-under-test.cpp',original,text,s=>s.replace('ymcc::radialfixture::createEnvironment(profile.c_str(),','CreateCoreWebView2EnvironmentWithOptions(nullptr,profile.c_str(),nullptr,'));entry=path.join(scope.out,'monitor-under-test.cpp');}
 save(path.join(scope.out,'native-transforms.json'),{pass:true,transforms,algorithmsAndAssertionsRestoredExactly:true,clockAndTimeoutsUnchanged:true});generated(scope,path.join(scope.out,'native-transforms.json'),'reversible transformation proof');return entry;
}
export async function compile(scope,entry,executableName){
 const s=scope.native.manifest.selected,exe=path.join(scope.out,executableName),q=value=>{need(!/["%\r\n]/.test(value),'COMPILER_PATH');return '"'+value+'"';};
 const args=['/nologo','/std:c++20','/utf-8','/EHsc','/O1','/MT','/DUNICODE','/D_UNICODE','/sourceDependencies '+q(path.join(scope.out,'native-dependencies.json')),q(entry),'/I'+q(scope.out),'/I'+q(path.join(scope.repo,'native')),'/I'+q(path.join(scope.repo,'deps/webview2/build/native/include')),'/I'+q(path.join(scope.repo,'deps/json')),'/Fo'+q(path.join(scope.out,'native.obj')),'/Fe'+q(exe),'/link /BREPRO /VERBOSE:LIB /SUBSYSTEM:CONSOLE '+q(path.join(scope.repo,s.staticLoader))+' '+s.linkLibraries.join(' ')];
 fs.writeFileSync(path.join(scope.out,'compile.rsp'),args.join('\r\n')+'\r\n');generated(scope,path.join(scope.out,'compile.rsp'),'actual compiler arguments');
 const env={SystemRoot:process.env.SystemRoot,windir:process.env.SystemRoot,TEMP:scope.out,TMP:scope.out,PATH:path.dirname(s.compiler)+';'+path.join(process.env.SystemRoot,'System32'),INCLUDE:s.includeDirectories.join(';'),LIB:s.libraryDirectories.join(';'),VSLANG:'1033'};
 const versions=await run(s.powershell,['-NoLogo','-NoProfile','-NonInteractive','-File',path.join(scope.repo,'tools/fixtures/radial_private/tool-versions.ps1'),'-Manifest',scope.native.manifestFile,'-Repo',scope.repo],{cwd:scope.out,env,windowsHide:true,timeout:10000,maxBuffer:2e6});fs.writeFileSync(path.join(scope.out,'tool-versions.json'),versions.stdout);generated(scope,path.join(scope.out,'tool-versions.json'),'explicit tool versions; no registry');
 let result;try{result=await run(s.compiler,['@compile.rsp'],{cwd:scope.out,env,windowsHide:true,timeout:120000,maxBuffer:4e6});}catch(error){fs.writeFileSync(path.join(scope.out,'compile.log'),String(error.stdout||'')+String(error.stderr||''));throw Error('RADIAL_PRIVATE_COMPILE_FAILED: inspect compile.log');}
 fs.writeFileSync(path.join(scope.out,'compile.log'),result.stdout+result.stderr);generated(scope,path.join(scope.out,'compile.log'),'actual compiler and linker trace');
 const dependencies=readJson(path.join(scope.out,'native-dependencies.json'));generated(scope,path.join(scope.out,'native-dependencies.json'),'actual MSVC include graph');
 const nativeInputs=[];
 for(const file of [dependencies.Data.Source,...dependencies.Data.Includes]){
  const actual=fs.realpathSync(file),pin=scope.native.pins.get(key(actual));
  if(inside(scope.out,actual)){need(scope.generated.some(r=>key(r.path)===key(actual)),'UNBOUND_GENERATED_INCLUDE:'+actual);}
  else if(inside(scope.repo,actual)&&!actual.toLowerCase().includes('node_modules'))source(scope,actual);
  else need(pin&&pin.sha256===sha(actual),'UNDECLARED_NATIVE_HEADER:'+actual);
  nativeInputs.push({path:actual,sha256:sha(actual),role:pin?'tool':inside(scope.out,actual)?'generated':'source'});
 }
 const linked=[];for(const match of (result.stdout+result.stderr).matchAll(/(?:[A-Z]:\\[^\r\n]*?\.lib)/gi)){
  const file=match[0].trim();if(!fs.existsSync(file))continue;
  const pin=scope.native.pins.get(key(file));need(pin&&pin.sha256===sha(file),'UNDECLARED_LINK_INPUT:'+file);linked.push({path:file,sha256:sha(file)});
 }
 need(linked.some(r=>key(r.path)===key(path.join(scope.repo,s.staticLoader))),'STATIC_LOADER_NOT_IN_ACTUAL_LINK_TRACE');
 generated(scope,path.join(scope.out,'native.obj'),'actual native object');generated(scope,exe,'actual private-desktop executable');
 save(path.join(scope.out,'native-build-graph.json'),{pass:true,sourceHashes:scope.sourceHashes,nativeInputs,linkedLibraries:unique(linked),compiler:s.compiler,linker:s.linker,executable:exe,executableSha256:sha(exe),toolProof:verify(scope)});generated(scope,path.join(scope.out,'native-build-graph.json'),'actual native build graph');return exe;
}
export function mergeRenderer(scope,file){
 const renderer=readJson(file);need(renderer.pass,'RENDERER_FAILED');
 for(const [relative,digest]of Object.entries(renderer.sourceHashes)){need(sha(path.isAbsolute(relative)?relative:path.join(scope.repo,relative))===digest,'RENDERER_CHANGED:'+relative);scope.sourceHashes[relative]=digest;}
 for(const row of renderer.compiledArtifacts??[])need(sha(row.path)===row.sha256,'RENDERER_ARTIFACT_CHANGED');
 for(const row of renderer.toolArtifacts??[])need(scope.toolArtifacts.some(pin=>key(pin.path)===key(row.path)&&pin.sha256===row.sha256),'RENDERER_TOOL_NOT_DECLARED');
}
export async function executeReviewed(scope,exe,web){
 const file=process.env.YMCC_RADIAL_EXECUTION_REVIEW,pin=process.env.YMCC_RADIAL_EXECUTION_REVIEW_SHA256;
 if(!file||!pin){const error=Error('NATIVE_PREEXECUTION_PARENT_REVIEW_REQUIRED');error.blocked=true;throw error;}
 need(/^[a-f0-9]{64}$/.test(pin)&&sha(file)===pin,'REVIEW_PIN');const approval=readJson(file);
 need(approval.schemaVersion===1&&approval.approved===true&&(approval.case===scope.entry||approval.cases?.includes(scope.entry))&&approval.builds?.some(row=>key(row.preparedOut)===key(scope.out)&&row.executableSha256===sha(exe)),'REVIEW_BUILD_IDENTITY');
 need(approval.nativeToolsSha256===sha(scope.native.manifestFile)&&approval.nodeGraphSha256===scope.node.graphSha256,'REVIEW_TOOL_IDENTITY');
 need(approval.allowedBoundary==='owned-private-desktop-composition-independent-input-and-explicit-webview-runtime'&&approval.runtimeOsConfinementReviewed===true,'REVIEW_UI_RUNTIME_CONFINEMENT');
 const runtime=path.resolve(approval.runtimeRoot);need(typeof approval.runtimeVersion==='string'&&/^\d+\.\d+\.\d+\.\d+$/.test(approval.runtimeVersion)&&Array.isArray(approval.runtimeFiles)&&approval.runtimeFiles.length>0,'REVIEW_FIXED_RUNTIME_FILES');
 const entries=fs.readdirSync(runtime,{recursive:true,withFileTypes:true});need(entries.every(e=>!e.isSymbolicLink()),'RUNTIME_DIRECTORY_REPARSE');const actualFiles=entries.filter(e=>e.isFile()).map(e=>path.join(e.parentPath||e.path,e.name));
 need(actualFiles.length===approval.runtimeFiles.length&&actualFiles.length<=LIMIT,'RUNTIME_MEMBERSHIP');
 for(const file of actualFiles){need(key(fs.realpathSync(file))===key(file),'RUNTIME_REPARSE');const row=approval.runtimeFiles.find(r=>key(path.resolve(runtime,r.path))===key(file));need(row?.sha256===sha(file),'RUNTIME_PIN:'+file);}
 need(actualFiles.some(f=>key(f)===key(path.join(runtime,'msedgewebview2.exe'))),'FIXED_RUNTIME_EXECUTABLE');
 need(unique([...scope.toolArtifacts,...scope.generated,...actualFiles.map(file=>({path:file,sha256:sha(file)}))]).length<=LIMIT,'RUNTIME_COMBINED_LIMIT');
 const env={...process.env,YMCC_RADIAL_FIXED_RUNTIME:runtime,YMCC_RADIAL_FIXED_RUNTIME_VERSION:approval.runtimeVersion};
 for(const name of Object.keys(env))if(/^WEBVIEW2_/i.test(name))delete env[name];
 scope.nativeLaunchAttempted=true;
 const execution=run(exe,[web,scope.out,path.join(scope.out,'frame.json')],{cwd:scope.out,env,windowsHide:true,timeout:150000,maxBuffer:4e6});
 execution.child?.once('spawn',()=>{scope.nativeExecutionStarted=true;});
 let result;try{result=await execution;scope.nativeExecutionStarted=true;}catch(error){if(execution.child?.pid)scope.nativeExecutionStarted=true;fs.writeFileSync(path.join(scope.out,'run.log'),String(error.stdout||'')+String(error.stderr||''));throw error;}
 fs.writeFileSync(path.join(scope.out,'run.log'),result.stdout+result.stderr);
 const after=fs.readdirSync(runtime,{recursive:true,withFileTypes:true});need(after.every(e=>!e.isSymbolicLink())&&after.filter(e=>e.isFile()).length===actualFiles.length,'RUNTIME_CHANGED_AFTER_EXECUTION');
 for(const file of actualFiles){const pin=approval.runtimeFiles.find(row=>key(path.resolve(runtime,row.path))===key(file));need(pin&&sha(file)===pin.sha256,'RUNTIME_BYTES_CHANGED_AFTER_EXECUTION');}
 return {reviewFile:file,reviewSha256:pin,runtimeRoot:runtime,runtimeVersion:approval.runtimeVersion,runtimeFiles:approval.runtimeFiles};
}
