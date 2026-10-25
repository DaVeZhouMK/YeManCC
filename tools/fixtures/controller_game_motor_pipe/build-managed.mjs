// Build-only SDK-free managed consumer for the real native pipe integration.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {createOfflineHostFixture} from '../../msi_steamdeck_release_fence_1726_selftest.mjs';
import {toolchainFiles} from '../../ymcc_sim/global/inputhost-protocol/adapter.mjs';

import {REPO as repo,WORKSPACE,MANAGED_SOURCES,FIXTURE,STALE_SOURCE,inside} from './contract.mjs';
import {staleRevision} from './faults.mjs';
const self=fileURLToPath(import.meta.url);
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const fileHash=p=>hash(fs.readFileSync(p));
const need=(ok,message)=>{if(!ok)throw Error(message);};
const json=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n',{flag:'wx'});
const replaceOnce=(s,a,b)=>{need(s.split(a).length===2,'Source anchor drift: '+a);return s.replace(a,b);};
function member(text,marker){
 const start=text.indexOf(marker);need(start>=0&&text.indexOf(marker,start+marker.length)<0,'Nonunique production member: '+marker);
 const open=text.indexOf('{',start);let depth=0,mode='code';
 for(let i=open;i<text.length;i++){
  const c=text[i],pair=text.slice(i,i+2);
  if(mode==='line'){if(c==='\n')mode='code';}
  else if(mode==='comment'){if(pair==='*/'){mode='code';i++;}}
  else if(mode==='string'||mode==='char'){if(c==='\\')i++;else if(c===(mode==='string'?'"':"'"))mode='code';}
  else if(pair==='//'){mode='line';i++;}else if(pair==='/*'){mode='comment';i++;}
  else if(c==='"')mode='string';else if(c==="'")mode='char';else if(c==='{')depth++;
  else if(c==='}'&&--depth===0)return{text:text.slice(start,i+1),start,end:i+1,line:text.slice(0,start).split('\n').length};
 }
 throw Error('Unclosed member: '+marker);
}
const xml=s=>s.replaceAll('&','&amp;').replaceAll('"','&quot;');
export function buildManagedFixture(out,{caseRoot,variant='normal'}={}){
 need(path.isAbsolute(out),'--out must be absolute');out=path.resolve(out);
 need(['normal','stale-revision'].includes(variant),'Unknown fixture variant');
 need(typeof caseRoot==='string'&&path.isAbsolute(caseRoot)&&inside(path.join(WORKSPACE,'Build'),caseRoot),'Case-owned Build root required');
 need(path.resolve(out)===path.resolve(caseRoot,'managed',variant),'Managed output must belong to this case and variant');
 need(!fs.existsSync(out),'Fresh output required: '+out);fs.mkdirSync(out,{recursive:true});
 const commands=[];
 function run(name,args){const r=spawnSync('dotnet',args,{cwd:out,encoding:'utf8',timeout:60000,windowsHide:true,env:{...process.env,DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_NOLOGO:'1',DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE:'true'}});fs.writeFileSync(path.join(out,name+'.log'),(r.stdout??'')+(r.stderr??''));commands.push({name,args,exit:r.status,error:r.error?.message??null});need(!r.error&&r.status===0,name+' failed: '+path.join(out,name+'.log'));return r.stdout;}
 try{
 const names=[...MANAGED_SOURCES];need(names.length===24&&new Set(names).size===24,'Exact24 managed sources required');
 const inputs=names.map(relative=>{const p=path.join(repo,relative),bytes=fs.readFileSync(p),snapshot=path.join(out,'input-sources',relative);fs.mkdirSync(path.dirname(snapshot),{recursive:true});fs.writeFileSync(snapshot,bytes,{flag:'wx'});return{path:p,relative,snapshot,sha256:hash(bytes),bytes:bytes.length};});
 if(variant==='stale-revision'){
  const original=inputs.find(r=>r.relative==='InputHost/HostGameOutputObservation.cs');
  const bytes=Buffer.from(staleRevision(fs.readFileSync(original.snapshot,'utf8').replace(/^\uFEFF/,'')),'utf8');
  const p=path.join(out,STALE_SOURCE),snapshot=path.join(out,'input-sources',STALE_SOURCE);
  fs.mkdirSync(path.dirname(p),{recursive:true});fs.mkdirSync(path.dirname(snapshot),{recursive:true});
  fs.writeFileSync(p,bytes,{flag:'wx'});fs.writeFileSync(snapshot,bytes,{flag:'wx'});
  inputs.push({path:p,relative:STALE_SOURCE,snapshot,sha256:hash(bytes),bytes:bytes.length,derivedFrom:original.path,originalSha256:original.sha256,privateSourceFault:true});
 }
 const read=relative=>fs.readFileSync(inputs.find(r=>r.relative===relative).snapshot,'utf8').replace(/^\uFEFF/,'');
 const tools=toolchainFiles(repo).map(p=>{need(fs.existsSync(p)&&fs.statSync(p).isFile(),'Missing compiler/framework input: '+p);return{path:p,sha256:fileHash(p),bytes:fs.statSync(p).size};});
 const source=read('InputHost/Program.cs'),host=path.join(repo,'InputHost');
 const generated=createOfflineHostFixture(out,host,true,{externalGameOutputHelper:true,externalGameFeedbackCatalog:true});let fake=generated.fake;
 fake=replaceOnce(fake,'public event Action<HMController,HMOutputPacket>? OutputReceived;','public event Action<HMController,HMOutputPacket>? OutputReceived; public void EmitFixtureOutput(byte source,byte report,ReadOnlyMemory<byte> data,uint sequence)=>OutputReceived?.Invoke(this,new HMOutputPacket((HMOutputSource)source,report,data,sequence)); public Action<HMController,HMOutputPacket>? SnapshotFixtureOutput()=>OutputReceived;');
 need(!/\bclass\s+HcControllerGameFeedbackCapabilityCatalog\b/.test(fake),'Game feedback catalog must be compiled separately, not embedded');
 // Preserve actual production protocol/core constants, while the old asset
 // verification endpoint remains a clearly declared fake.
 const constants=['LockedCoreSha256','LockedCoreMvid'].map(name=>{const matches=[...source.matchAll(new RegExp('private const string '+name+'\\s*=\\s*[^;]+;','g'))];need(matches.length===1,'Constant missing: '+name);return matches[0][0];});
 const byteConstants=[...source.matchAll(/^    private const long LockedCoreBytes\s*=\s*[^;]+;/gm)];need(byteConstants.length===1,'Current LockedCoreBytes declaration missing');
 constants.push(byteConstants[0][0]);
 fake=replaceOnce(fake,'private const string LockedCoreSha256="fake-no-sdk",LockedCoreMvid="fake-no-sdk";',constants.join('\n'));
 const supportFields=[
  ['_psEndpointOwnership',/^        private readonly PsEndpointOwnership _psEndpointOwnership = new\(\);\r?$/gm],
  ['_runtimeBinaryIdentity',/^        private HostRuntimeBinaryIdentity\.Snapshot\? _runtimeBinaryIdentity;\r?$/gm]
 ].map(([marker,pattern])=>{const matches=[...source.matchAll(pattern)];need(matches.length===1,'Actual current Host field missing:'+marker);const m=matches[0];return{text:m[0],marker,start:m.index,end:m.index+m[0].length,line:source.slice(0,m.index).split('\n').length};});
 const supportMethods=['private PsEndpointOwnership.Binding PsOwnershipBinding(','private void CapturePsEndpointOwnership(','private string AppendPsEndpointOwnership('].map(marker=>({...member(source,marker),marker}));
 const supportReader={...member(source,'private sealed class PsOwnershipCmReader'),marker:'private sealed class PsOwnershipCmReader'};
 fake=replaceOnce(fake,'private sealed partial class HostSession {','private sealed partial class HostSession {\n'+[...supportFields,...supportMethods].map(r=>r.text).join('\n')+'\n');
 // Exact production reader and helper remain active. Only its CM IO leaves are
 // fail-closed terminals: even an unexpected call can never enumerate this PC.
 const cmTerminals=[
  'private static int CM_Locate_DevNodeW(out uint node,string id,uint flags)=>throw new InvalidOperationException("fixture-cm-io-forbidden");',
  'private static int CM_Get_Child(out uint child,uint node,uint flags)=>throw new InvalidOperationException("fixture-cm-io-forbidden");',
  'private static int CM_Get_Sibling(out uint sibling,uint node,uint flags)=>throw new InvalidOperationException("fixture-cm-io-forbidden");',
  'private static int CM_Get_Device_IDW(uint node,StringBuilder buffer,int count,uint flags)=>throw new InvalidOperationException("fixture-cm-io-forbidden");',
  'private static int CM_Get_DevNode_Status(out uint status,out uint problem,uint node,uint flags)=>throw new InvalidOperationException("fixture-cm-io-forbidden");'
 ].join('\n');
 fake=replaceOnce(fake,'internal static partial class Program {','internal static partial class Program {\n'+supportReader.text+'\n'+cmTerminals+'\n');
 const ownershipSource=read('InputHost/PsEndpointOwnership.cs');
 need(ownershipSource.includes('if (!assetVerified) return Fail("sdk-asset-unverified");')&&ownershipSource.indexOf('sdk-type-metadata-mismatch')<ownershipSource.indexOf('cm.Locate('),'Actual ownership guard ordering changed');
 need(!/\bclass\s+PsEndpointOwnership\b/.test(fake)&&! /\bclass\s+HostRuntimeBinaryIdentity\b/.test(fake),'Actual new helpers must compile as separate original source files');
 const productionDependencySupport={actualOwnershipClass:'InputHost/PsEndpointOwnership.cs',actualRuntimeIdentityClass:'InputHost/HostRuntimeBinaryIdentity.cs',actualReaderAndCaptureAppend:true,cmIoBoundary:'All five Configuration Manager terminal methods throw; no PnP or full-machine enumeration can execute.',sdkMetadataNotForged:true,ownershipIncompleteIsNotAdmission:true,newHelperBodiesStubbed:false};


 let legacy=read('tools/msi_steamdeck_release_fence_1726_selftest.cs');
 legacy=replaceOnce(legacy,'internal static class Fixture','internal static partial class Fixture');
 legacy=replaceOnce(legacy,'private static int Main()','private static int UnusedLegacySelfTestEntry()');
 let compatibility=read('tools/inputhost_lifecycle_selftest.cs');
 const supportDuplicates=[];
 for(const [name,pattern]of [
  ['LockedCoreBytes',/^[ \t]*private const long LockedCoreBytes\s*=\s*[^;]+;\r?$/gm],
  ['_runtimeBinaryIdentity',/^[ \t]*private HostRuntimeBinaryIdentity\.Snapshot\? _runtimeBinaryIdentity;\r?$/gm]
 ]){const matches=[...compatibility.matchAll(pattern)];need(matches.length<=1,'Multiple compatibility support declarations:'+name);if(matches.length===1){supportDuplicates.push({name,removedFromGeneratedCompatibilityOnly:true,originalDeclarationSha256:hash(matches[0][0]),actualProgramDeclarationRetained:true});compatibility=replaceOnce(compatibility,matches[0][0],'');}}
 productionDependencySupport.compatibilityDeclarationDeduplication=supportDuplicates;

 const runtimeShadow=member(compatibility,'private static class HostRuntimeBinaryIdentity');
 compatibility=replaceOnce(compatibility,runtimeShadow.text,'');
 productionDependencySupport.runtimeShadowRemovedFromGeneratedCompatibility={sha256:hash(runtimeShadow.text),originalSourceUnchanged:true,actualNamespaceHelperNowResolves:true};
 need(!/\bclass\s+(?:HostRuntimeBinaryIdentity|PsEndpointOwnership)\b/.test(compatibility),'No shadow may replace the actual helper');

 compatibility=replaceOnce(compatibility,'private sealed class Stage','private sealed partial class Stage');
 compatibility=replaceOnce(compatibility,'public void Decode(string a,ulong b,string c,ulong d,ulong e,string f){}','public void Decode(string a,ulong b,string c,ulong d,ulong e,string f){ManagedDecode(a,b,c,d,e,f);}');
 compatibility=replaceOnce(compatibility,'return Fixture.OutputStops;','return ManagedOutputStopResult(Fixture.OutputStops);');
 const pieces=['private static async Task<int> RunPipeClientAsync(','private sealed class MessageCodec'].map(marker=>({...member(source,marker),marker}));
 const files={
 'ExtractedHost.cs':fake,'FixtureTerminals.cs':compatibility,'LegacyFixture.cs':legacy,
 'ManagedEntry.cs':read(FIXTURE+'/ManagedEntry.cs'),
 'ManagedGameMotorFixture.cs':read(FIXTURE+'/ManagedGameMotorFixture.cs'),
 'ProductionPipe.cs':'using System.Diagnostics; using System.IO.Pipes; using System.Text;\ninternal static partial class Program {\n'+pieces.map(r=>r.text).join('\n')+'\n}\n',
 'ParentProcessLifetime.cs':read('InputHost/ParentProcessLifetime.cs')
 };
 for(const name of ['MsiSteamDeckReleaseFence1726.cs','MsiVirtualReportChain1410.cs','CanonicalFrameFingerprintMemo.cs','HostResponseFields.cs','HostGameOutputObservation.cs','PsEndpointOwnership.cs','HostRuntimeBinaryIdentity.cs'])files[name]=read('InputHost/'+name);
 if(variant==='stale-revision')files['HostGameOutputObservation.cs']=read(STALE_SOURCE);
 files['HcControllerGameFeedbackCapabilityCatalog.generated.cs']=read('capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs');
 for(const [name,text]of Object.entries(files))fs.writeFileSync(path.join(out,name),text,{flag:'wx'});
 const project=path.join(out,'Fixture.csproj'),dll=path.join(out,'bin/Release/net10.0/Fixture.dll');
 const compileNames=[...Object.keys(files),'BuildIdentity.g.cs'];
 const projectText=`<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><AssemblyName>Fixture</AssemblyName><StartupObject>Program</StartupObject><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><UseAppHost>false</UseAppHost><NuGetAudit>false</NuGetAudit><EnableNETAnalyzers>false</EnableNETAnalyzers><EnableDefaultCompileItems>false</EnableDefaultCompileItems><ImportDirectoryBuildProps>false</ImportDirectoryBuildProps><ImportDirectoryBuildTargets>false</ImportDirectoryBuildTargets><UseSharedCompilation>false</UseSharedCompilation><Deterministic>true</Deterministic></PropertyGroup><ItemGroup>${compileNames.map(n=>'<Compile Include="'+xml(n)+'"/>').join('')}</ItemGroup><Import Project="FixtureInputs.targets"/></Project>\n`;
 fs.writeFileSync(project,projectText,{flag:'wx'});
 fs.writeFileSync(path.join(out,'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>\n',{flag:'wx'});
 fs.writeFileSync(path.join(out,'FixtureInputs.targets'),`<Project><Target Name="RecordManagedFixtureInputs" AfterTargets="CoreCompile"><ItemGroup><_ManagedImports Include="$(MSBuildAllProjects)"/></ItemGroup><WriteLinesToFile File="$(MSBuildProjectDirectory)/evaluated-inputs.txt" Lines="@(Compile-&gt;'Compile&#x9;%(FullPath)');@(ReferencePath-&gt;'Reference&#x9;%(FullPath)');@(Analyzer-&gt;'Analyzer&#x9;%(FullPath)');@(_ManagedImports-&gt;'Import&#x9;%(FullPath)')" Overwrite="true" Encoding="UTF-8"/></Target></Project>\n`,{flag:'wx'});
 const generatedInputs=[...Object.keys(files),'Fixture.csproj','NuGet.Config','FixtureInputs.targets'].map(name=>{const p=path.join(out,name);return{path:p,sha256:fileHash(p),bytes:fs.statSync(p).size};});
 const bindings={schemaVersion:1,variant,caseRoot,sources:inputs,generatedInputs,toolchainAllowlist:tools,ranges:[...generated.receipt,...[...pieces,...supportFields,...supportMethods,supportReader].map(({text,...r})=>({...r,file:path.join(host,'Program.cs'),sha256:hash(text),copiedVerbatim:true}))],productionDependencySupport,constants:constants.join('\n'),scope:{productionPipeLoop:'verbatim',productionMessageCodec:'verbatim',productionParentLifetime:'verbatim',hostSession:'Existing actual-consumer extraction; existing fake clock replacements retained. Handle, HandleLocked, ReleaseLocal and ReleaseLocalLocked are not stubbed.',terminals:'Existing SDK/PnP/neutral/frame/diagnostic fakes reused. Stage diagnostic observation and one-shot fake output-stop result extended only in generated copies.',normalHostMainExecuted:false,realSdkLoaded:false,hardwareIo:false,productFilesEdited:false},toolchainMeaning:'Declared compiler/framework allowlist, not all files are claimed loaded; evaluated-inputs.txt identifies actual Compile/Reference/Analyzer/Import rows.'};
 const bindingText=JSON.stringify(bindings),bindingHash=hash(bindingText);
 fs.writeFileSync(path.join(out,'BuildIdentity.g.cs'),'internal static class ManagedFixtureIdentity { internal const string InputManifestSha256="'+bindingHash+'"; }\n',{flag:'wx'});
 const sdkVersion=run('sdk-version',['--version']).trim();
 run('build',['build',project,'-c','Release','--nologo','-v:minimal']);
 const ledger=fs.readFileSync(path.join(out,'evaluated-inputs.txt'),'utf8').replace(/^\uFEFF/,'');
 const actual=ledger.split(/\r?\n/).filter(Boolean).map(line=>{const [kind,...parts]=line.split('\t'),p=path.resolve(parts.join('\t'));need(['Compile','Reference','Analyzer','Import'].includes(kind)&&fs.existsSync(p),'Unbound actual compile input: '+line);return{kind,path:p,sha256:fileHash(p)};});
 need(actual.filter(r=>r.kind==='Compile').length===19&&actual.some(r=>r.kind==='Reference'),'Exact19 actual compiler inputs required');
 const observationInput=inputs.find(r=>r.relative===(variant==='stale-revision'?STALE_SOURCE:'InputHost/HostGameOutputObservation.cs'));
 const observationCompile=actual.filter(r=>r.kind==='Compile'&&path.resolve(r.path)===path.resolve(out,'HostGameOutputObservation.cs'));
 need(observationCompile.length===1&&observationCompile[0].sha256===observationInput.sha256,'Observation helper missing or changed in actual compiler inputs');
 const gameFeedbackCatalogInput=inputs.find(r=>r.relative==='capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs');
 const gameFeedbackCatalogCompile=actual.filter(r=>r.kind==='Compile'&&path.resolve(r.path)===path.resolve(out,'HcControllerGameFeedbackCapabilityCatalog.generated.cs'));
 need(gameFeedbackCatalogCompile.length===1&&gameFeedbackCatalogCompile[0].sha256===gameFeedbackCatalogInput.sha256,'Game feedback catalog missing or changed in actual compiler inputs');
 const declared=new Map(tools.map(r=>[r.path.toLowerCase(),r]));
 for(const row of actual){const r=path.relative(out,row.path);if(r==='..'||r.startsWith('..'+path.sep)||path.isAbsolute(r)){need(declared.has(row.path.toLowerCase()),'Compiler input outside declared allowlist: '+row.path);need(declared.get(row.path.toLowerCase()).sha256===row.sha256,'Compiler input changed: '+row.path);}}
 const sourceStable=inputs.every(r=>fileHash(r.path)===r.sha256&&fileHash(r.snapshot)===r.sha256),toolchainStable=tools.every(r=>fileHash(r.path)===r.sha256);need(sourceStable&&toolchainStable,'Input changed during build');
 need(generatedInputs.every(r=>fileHash(r.path)===r.sha256),'Generated compilation input changed during build');
 const runtimeArtifacts=['Fixture.dll','Fixture.deps.json','Fixture.runtimeconfig.json'].map(name=>{const p=path.join(path.dirname(dll),name);return{path:p,sha256:fileHash(p),bytes:fs.statSync(p).size};});
 const result={schemaVersion:1,variant,caseRoot,status:'BUILT',behaviorPass:null,fixtureExecuted:false,fixtureProject:project,fixtureDll:dll,manifestPath:path.join(path.dirname(dll),'fixture-manifest.json'),inputManifestSha256:bindingHash,sourceStable,toolchainStable,sdkVersion,commands,runtimeArtifacts,evaluatedCompilerInputs:actual,generatedIdentity:{path:path.join(out,'BuildIdentity.g.cs'),sha256:fileHash(path.join(out,'BuildIdentity.g.cs'))},normalHostMainExecuted:false,realSdkLoaded:false,hardwareIo:false,sourceFilesEdited:false,cli:['dotnet',dll,'--pipe','NAME','--parent-pid','PID','--receipt','ABS_NEW_JSON','[--fault output-stop-once]'],receiptContract:['hasController','hasContext','phase','events','faultTriggered','pipeExitCode','outputStopResults','stateAfterLoop','stateAfterFinalCleanup','inputBindings'],boundaries:['Compiled fixture only. Behavioral/natural-exit conclusions require the native parent to run scenarios and observe process exit.','SDK SHA/MVID in replies are current product constants; the admitted asset verifier fake does not load/verify the actual SDK.','Only BCL runtime and the generated fixture assembly are loaded; no device/driver/Steam calls.']};
 const manifestText=JSON.stringify(result).slice(0,-1)+',"inputBindings":'+bindingText+'}\n';
 fs.writeFileSync(result.manifestPath,manifestText,{flag:'wx'});fs.writeFileSync(path.join(out,'fixture-manifest.json'),manifestText,{flag:'wx'});
 const output={status:result.status,fixtureProject:project,fixtureDll:dll,manifestPath:result.manifestPath,manifestSha256:fileHash(result.manifestPath),runtimeArtifacts,sourceStable,toolchainStable,sdkVersion};
 json(path.join(out,'build-result.json'),output);return output;
 }catch(error){json(path.join(out,'failure.json'),{status:'INCOMPLETE',error:error.message,commands,normalHostMainExecuted:false,realSdkLoaded:false,hardwareIo:false});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===self){
 try{const args=process.argv.slice(2);need(args.length===6&&args[0]==='--case-root'&&args[2]==='--variant'&&args[4]==='--out','Use --case-root ABS --variant normal|stale-revision --out ABS');console.log(JSON.stringify(buildManagedFixture(args[5],{caseRoot:args[1],variant:args[3]}),null,2));}catch(error){console.error(error.stack??error.message);process.exitCode=2;}
}
