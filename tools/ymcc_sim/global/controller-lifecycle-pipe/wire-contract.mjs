// Fixed direct-tool contract for the existing SDK-free native wire consumer.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const WIRE_ID='controller.lifecycle-native-managed-pipe';
export const WIRE_CONTRACT_SOURCE='tools/ymcc_sim/global/controller-lifecycle-pipe/wire-contract.mjs';
const need=(ok,message)=>{if(!ok)throw Error('WIRE_CONTRACT_'+message);};
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const inside=(root,file)=>{const rel=path.relative(root,file);return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));};
const uniqueRoot=(files,pattern,name)=>{const found=[...new Set(files.flatMap(file=>{const match=path.resolve(file).split(path.sep).join('/').match(pattern);return match?[match[1]]:[];}))];need(found.length===1,name+'_NOT_UNIQUE');return path.resolve(found[0]);};
export function resolveWireBuild(repoRoot=REPO){
 const native=read(path.join(repoRoot,'tools/ymcc_sim/global/controller-lifecycle-pipe/native-tool-inputs.json')).files;
 const shared=read(path.join(repoRoot,'tools/ymcc_sim/global/controller-priority-ledger/tool-inputs.json')).files;
 need(Array.isArray(native)&&Array.isArray(shared),'TOOL_LIST_INVALID');
 // Fixed versioned paths are part of the source ledger. Installed directory
 // enumeration, vcvars defaults and inherited SDK/MSVC variables cannot select.
 const msvcRoot=uniqueRoot(native,/^(.*[\/]VC[\/]Tools[\/]MSVC[\/]\d+\.\d+\.\d+)[\/]/i,'MSVC');
 const sdkInclude=uniqueRoot(native,/^(.*[\/]Windows Kits[\/]10[\/]Include[\/]\d+(?:\.\d+){3})[\/]/i,'SDK');
 const sdkVersion=path.basename(sdkInclude),sdkRoot=path.dirname(path.dirname(sdkInclude));
 const windowsRoot=path.resolve('C:/Windows'),bin=path.join(msvcRoot,'bin/Hostx64/x64');
 const includePaths=[...['ucrt','shared','um','winrt','cppwinrt'].map(name=>path.join(sdkInclude,name)),path.join(msvcRoot,'include')];
 const libraryPaths=[...['ucrt','um'].map(name=>path.join(sdkRoot,'Lib',sdkVersion,name,'x64')),path.join(msvcRoot,'lib/x64')];
 const value={schemaVersion:1,kind:'wire-fixed-direct-msvc-sdk-v1',architecture:'x64',msvcRoot,sdkRoot,sdkVersion,windowsRoot,compiler:path.join(bin,'cl.exe'),linker:path.join(bin,'link.exe'),cmd:path.join(windowsRoot,'System32/cmd.exe'),includePaths,libraryPaths,dotnet:path.resolve('C:/Program Files/dotnet/dotnet.exe'),node:process.execPath};
 for(const file of [value.compiler,value.linker,value.cmd,value.dotnet,value.node,...includePaths,...libraryPaths])need(fs.existsSync(file),'PINNED_PATH_MISSING:'+file);
 for(const file of [value.compiler,value.linker])need(shared.some(item=>path.resolve(item).toLowerCase()===file.toLowerCase()),'COMPILER_NOT_IN_LEDGER:'+file);
 need(!JSON.stringify(value).match(/[\r\n%]/),'PATH_METACHARACTER');
 return value;
}
export function wireChildEnvironment(build,privateRoot){
 const base=path.resolve(privateRoot),bin=path.dirname(build.compiler);
 // Construct from nothing: no inherited CL/LINK/SDK/MSVC/Node or DOTNET probes.
 return {SystemRoot:build.windowsRoot,WINDIR:build.windowsRoot,COMSPEC:build.cmd,
  PATH:[bin,path.join(build.windowsRoot,'System32'),build.windowsRoot].join(';'),PATHEXT:'.COM;.EXE;.BAT;.CMD',
  INCLUDE:build.includePaths.join(';'),LIB:build.libraryPaths.join(';'),LIBPATH:build.libraryPaths.join(';'),
  TEMP:base,TMP:base,LOCALAPPDATA:base,APPDATA:base,USERPROFILE:base,DOTNET_CLI_HOME:base,
  DOTNET_ROOT:path.dirname(build.dotnet),DOTNET_MULTILEVEL_LOOKUP:'0',DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_NOLOGO:'1',DOTNET_DISABLE_GUI_ERRORS:'1'};
}
export function wireCompilerPreamble(build){
 return '@echo off\r\n'; // INCLUDE/LIB/PATH are the explicit child environment.
}
export function wirePathContext(outputRoot,coreRoot=path.resolve(REPO,'../../../Testing/YMCC')){
 const root=path.resolve(outputRoot),core=path.resolve(coreRoot);
 need(path.isAbsolute(outputRoot)&&inside(core,root),'OUTPUT_OUTSIDE_CORE');
 need(!/["%\r\n]/.test(root),'OUTPUT_UNSUPPORTED_CHARACTERS');
 let existing=root;while(!fs.existsSync(existing))existing=path.dirname(existing);
 need(fs.realpathSync(existing).toLowerCase()===existing.toLowerCase(),'OUTPUT_REDIRECTION');
 // Bound the actual generated native compile path class, not a per-run UUID.
 const suffixes=['p/source/HcControllerFeedbackCapabilityCatalog.generated.h','n0/source/HcControllerFeedbackCapabilityCatalog.generated.h','n1/source/HcControllerFeedbackCapabilityCatalog.generated.h','n1/Checked/run/parent-kill-retry-dualsense-edge.json'];
 const longest=Math.max(...suffixes.map(suffix=>path.join(root,suffix).length));
 need(longest<=245,'OUTPUT_PATH_TOO_LONG');
 return {schemaVersion:1,kind:'owned-core-wire-short-path-v1',maximumNativePathChars:245,internalDirectories:['p','n0','n1'],outputEnvironment:'private-root-only;no-inherited-user-state'};
}
export function wireDescriptorContract(adapter,outputRoot,repoRoot=REPO){
 if(adapter?.id!==WIRE_ID)return null;
 need(adapter.kind==='node'&&adapter.path==='tools/ymcc_sim/global/controller-lifecycle-pipe/run.mjs'&&JSON.stringify(adapter.args)===JSON.stringify(['--evidence-dir','{outDir}/lifecycle-pipe']),'DESCRIPTOR_SCOPE_CHANGED');
 const build=resolveWireBuild(repoRoot),context=wirePathContext(outputRoot,path.resolve(repoRoot,'../../../Testing/YMCC'));
 need(adapter.evidenceInputs.includes(WIRE_CONTRACT_SOURCE),'DESCRIPTOR_CONTRACT_SOURCE_MISSING');
 return {schemaVersion:1,kind:'reviewed-native-wire-cache-v2-source-projection',build,pathClass:context,inheritance:'none;fixed-child-environment',artifactClosure:'all-original-observed-includes-libraries-and-loaded-managed-assemblies-verified',managedBuild:'explicit-source-bound-precompiled-only'};
}
