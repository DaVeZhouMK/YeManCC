// Build/tool observation only. Does not start a target binary or load target DLLs.
import fs from 'node:fs';import path from 'node:path';import {spawnSync} from 'node:child_process';import {createHash} from 'node:crypto';
import {calibrationToolchainPreamble} from '../../../fixtures/hc_reviewed_calibration/toolchain.mjs';
const sha=b=>createHash('sha256').update(b).digest('hex');export const fileIdentity=p=>({path:path.resolve(p),sha256:sha(fs.readFileSync(p)),bytes:fs.statSync(p).size});
export function buildToolRun(file,args,{cwd,log,timeout=60000,env={}}){const start=Date.now(),result=spawnSync(file,args,{cwd,env:{...process.env,DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_NOLOGO:'1',DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE:'true',...env},encoding:'utf8',windowsHide:true,timeout,maxBuffer:16*1024*1024});fs.writeFileSync(log,(result.stdout??'')+(result.stderr??''));const command={file,args,cwd,exitCode:result.status,signal:result.signal??null,error:result.error?.message??null,durationMs:Date.now()-start,log:fileIdentity(log),kind:'build-or-metadata-tool-only'};if(result.status!==0||result.error||result.signal){const error=new Error('BUILD_TOOL_FAILED:'+file+':'+log);error.command=command;throw error;}return {command,stdout:result.stdout??''};}
export function inspectNativeTools(out){fs.mkdirSync(out,{recursive:true});const vc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat',script=path.join(out,'tools.cmd');const text=calibrationToolchainPreamble(vc)+'where cl.exe\r\nwhere link.exe\r\nwhere dumpbin.exe\r\nset VCToolsInstallDir\r\nset WindowsSdkDir\r\nset WindowsSDKVersion\r\nset LIB\r\nset INCLUDE\r\n';fs.writeFileSync(script,text);const run=buildToolRun('cmd.exe',['/d','/c',script],{cwd:out,log:path.join(out,'tools.log')});const lines=run.stdout.split(/\r?\n/),vars=Object.fromEntries(lines.filter(line=>/^(VCToolsInstallDir|WindowsSdkDir|WindowsSDKVersion|LIB|INCLUDE)=/.test(line)).map(line=>{const at=line.indexOf('=');return [line.slice(0,at),line.slice(at+1)];}));const exes=lines.filter(line=>/^[A-Z]:\\.*(?:cl|link|dumpbin)\.exe$/i.test(line)).map(p=>p.trim());if(exes.length!==3)throw Error('EXACT_NATIVE_TOOLS_REQUIRED');const [cl,link,dumpbin]=exes;
 const toolDir=path.dirname(cl),tools=fs.readdirSync(toolDir).filter(n=>/^(?:cl|c1xx|c1|c2|link|mspdbsrv|dumpbin)\.exe$|^(?:mspdb|msobj|mspft|vcruntime|msvcp|pgodb).*\.dll$/i.test(n)).map(n=>fileIdentity(path.join(toolDir,n)));tools.push(fileIdentity(vc));return {command:run.command,vars,cl,link,dumpbin,tools,script:fileIdentity(script)};}
export function resolveLinkGraph(native,tooling,sourceRoot,{observationOut}={}){
 const dir=path.dirname(native.exe.path),logs=observationOut?path.resolve(observationOut):dir;
 if(observationOut){if(fs.existsSync(logs))throw Error('FRESH_LINK_OBSERVATION_OUTPUT_REQUIRED');fs.mkdirSync(logs,{recursive:true});}
 const script=fs.readFileSync(native.buildScript.path,'utf8');
 const explicit=[...script.matchAll(/"([^"\r\n]+\.lib)"/g)].map(m=>m[1]);
 const bare=[...script.matchAll(/(?:^|\s)([A-Za-z0-9_]+\.lib)(?=\s|\r|\n)/g)].map(m=>m[1]);
 const expand=p=>p.replaceAll('%WindowsSdkDir%',tooling.vars.WindowsSdkDir).replaceAll('%WindowsSDKVersion%',tooling.vars.WindowsSDKVersion);
 const libraryPaths=tooling.vars.LIB.split(';').filter(Boolean),pathKey=file=>path.resolve(file).toLowerCase();
 function resolve(name,reason){const filename=name.toLowerCase().endsWith('.lib')?name:name+'.lib';const found=libraryPaths.map(base=>path.join(base,filename)).find(fs.existsSync);if(!found)throw Error(reason+':'+filename);return {...fileIdentity(found),name,resolution:'recorded original LIB search order'};}
 const explicitFiles=explicit.map(expand).map(fileIdentity),resolved=bare.map(name=>resolve(name,'LINK_LIBRARY_MISSING'));
 const objectFiles=[...new Map([...script.matchAll(/"([^"\r\n]+\.obj)"/g)].map(m=>[pathKey(expand(m[1])),expand(m[1])])).values()];
 for(const file of [path.join(dir,'fixture.obj'),path.join(dir,'dsu.obj')])if(!objectFiles.some(p=>pathKey(p)===pathKey(file)))objectFiles.push(file);
 const objectInputs=objectFiles.map(fileIdentity);
 const directives=buildToolRun(tooling.dumpbin,['/nologo','/directives',...objectFiles],{cwd:dir,log:path.join(logs,'object-directives.log')});
 for(const p of objectInputs)if(fileIdentity(p.path).sha256!==p.sha256)throw Error('LINK_OBJECT_CHANGED_DURING_OBSERVATION:'+p.path);
 const names=text=>[...new Set([...text.matchAll(/DEFAULTLIB:\s*"?([^\s"\r\n]+)/gi)].map(m=>m[1].toLowerCase()))];
 const defaultsResolved=names(directives.stdout).map(name=>({...resolve(name,'DEFAULT_LINK_LIBRARY_MISSING'),source:'actual input object DEFAULTLIB directive'}));
 // Archive members may request CRT/system libraries not named by any input
 // object. Observe those directives too. This is a conservative metadata
 // closure: it does not claim every archive member was selected by LINK.
 const queue=[...explicitFiles,...resolved,...defaultsResolved],seen=new Set(),defaultKeys=new Set(defaultsResolved.map(p=>pathKey(p.path))),archiveObservations=[];
 for(let index=0;index<queue.length;++index){
  const library=queue[index],key=pathKey(library.path);if(seen.has(key))continue;seen.add(key);
  if(seen.size>256)throw Error('LINK_ARCHIVE_DIRECTIVE_CLOSURE_LIMIT256');
  if(fileIdentity(library.path).sha256!==library.sha256)throw Error('LINK_ARCHIVE_CHANGED_BEFORE_OBSERVATION:'+library.path);
  const observation=buildToolRun(tooling.dumpbin,['/nologo','/directives',library.path],{cwd:dir,log:path.join(logs,'archive-directives-'+archiveObservations.length+'.log')});
  if(fileIdentity(library.path).sha256!==library.sha256)throw Error('LINK_ARCHIVE_CHANGED_DURING_OBSERVATION:'+library.path);
  const dependencies=names(observation.stdout).map(name=>resolve(name,'TRANSITIVE_DEFAULT_LINK_LIBRARY_MISSING'));
  archiveObservations.push({archive:library,command:observation.command,dependencies});
  for(const dependency of dependencies){const dependencyKey=pathKey(dependency.path);if(!defaultKeys.has(dependencyKey)){defaultKeys.add(dependencyKey);defaultsResolved.push({...dependency,source:'archive-member DEFAULTLIB metadata; conservative transitive closure',requestedBy:library.path});}queue.push(dependency);}
 }
 const imports=buildToolRun(tooling.dumpbin,['/nologo','/headers','/imports',native.exe.path],{cwd:dir,log:path.join(logs,'pe-headers-imports.log')});
 const sm=JSON.parse(fs.readFileSync(native.sourceManifest.path,'utf8')),includes=sm.observedIncludePaths.map(fileIdentity);
 return {translationUnits:[{source:path.join(dir,'fixture.cpp'),object:fileIdentity(path.join(dir,'fixture.obj'))},{source:path.join(dir,'source/native/dsu_server.cpp'),object:fileIdentity(path.join(dir,'dsu.obj'))}],compiledIncludes:includes,explicitLibraries:explicitFiles,linkOperands:resolved,defaultLibraries:defaultsResolved,allObservedLinkObjectInputs:objectInputs,archiveDirectiveObservations:archiveObservations,directivesCommand:directives.command,peInspectionCommand:imports.command,tooling,sourceRoot,originalBuildScript:native.buildScript,linkObservationLimit:'All build-script objects plus recursive DEFAULTLIB metadata from declared and resolved archives in original LIB order. Archive closure is conservative; selected members, actual loader modules and runtime execution are not proved.'};
}
