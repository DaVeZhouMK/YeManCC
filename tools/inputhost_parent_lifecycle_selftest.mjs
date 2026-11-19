import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import os from 'node:os';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {extract} from './ymcc_sim/global/inputhost-protocol/extract-parser.mjs';
import {buildTargets,recordEvaluatedInputs} from './ymcc_sim/global/inputhost-protocol/provenance.mjs';
import {managedFixtureEnvironment} from './ymcc_sim/global/controller-signal-evidence/environment.mjs';
import {readInputHostRuntimePool,runtimePoolProbeSource,readRuntimePoolReceipts} from './lib/inputhost_runtime_pool.mjs';
const SELF=fs.realpathSync.native(fileURLToPath(import.meta.url));
const repo=path.resolve(path.dirname(SELF),'..');
const knownCases=['healthy-idle','pipe-break','parent-loss-during-read','parent-loss-during-write','parent-loss-during-connect','pipe-break-log-blocked','handle-exception-log-blocked','parent-loss-log-blocked','shutdown-log-blocked','release-failure-log-blocked','terminal-foreground-tail','terminal-release-blocked','terminal-log-blocked','terminal-parent-loss-in-handle','terminal-healthy-unarmed'];
// Preserve the original registered default's cost and negative-case contract.
// New process-exit boundaries use this same runner with explicit --case only.
const defaultCases=knownCases.slice(0,10);
export function parseInputHostParentLifecycleArgs(args){
 if(!Array.isArray(args))throw Error('Parent lifecycle arguments must be an array');
 const allowed=['--out','--source-root','--runtime-pool','--fault','--case','--configs'],values=new Map();
 for(let i=0;i<args.length;i++){
  const name=args[i];
  if(!allowed.includes(name)||values.has(name))throw Error('Unknown or duplicate parent lifecycle option: '+name);
  const value=args[++i];
  if(typeof value!=='string'||!value.trim()||value.startsWith('--'))throw Error('Missing or empty parent lifecycle value: '+name);
  values.set(name,value);
 }
 const selected=values.has('--case')?values.get('--case').split(','):null;
 if(selected&&(new Set(selected).size!==selected.length||selected.some(c=>!knownCases.includes(c))))throw Error('Invalid parent lifecycle selection');
 const configs=(values.get('--configs')??'Release,Debug').split(',');
 if(!configs.length||configs.some(c=>!['Release','Debug'].includes(c))||new Set(configs).size!==configs.length)throw Error('Invalid parent lifecycle configurations');
 if(!['none','ignore-parent-cancellation','ignore-terminal-deadline','return-without-process-exit'].includes(values.get('--fault')??'none'))throw Error('Unsupported parent fault');
 if(!['product','windows','portable'].includes(values.get('--runtime-pool')??'product'))throw Error('Unknown parent runtime pool');
 return values;
}
function execute(rawArgs){
const values=parseInputHostParentLifecycleArgs(rawArgs),option=(key,fallback)=>values.has(key)?values.get(key):fallback;
const out=path.resolve(option('--out','G:/YeManCC-Work/Build/Validation/VirtualController-Lifecycle-20261019/host/parent-current'));
const host=path.resolve(option('--source-root',path.join(repo,'InputHost')));
const poolProject=path.join(repo,'InputHost/YeManInputHost.csproj');
const runtimePool=readInputHostRuntimePool(poolProject,option('--runtime-pool','product'));
if(fs.existsSync(out))throw Error('Fresh output required: '+out);fs.mkdirSync(path.dirname(out),{recursive:true});fs.mkdirSync(out);
const inputs=[path.join(host,'Program.cs'),path.join(host,'ParentProcessLifetime.cs'),path.join(repo,'tools/inputhost_parent_lifecycle_selftest.cs'),SELF,path.join(repo,'tools/ymcc_sim/global/inputhost-protocol/extract-parser.mjs'),path.join(host,'HostTerminalCleanup.cs'),poolProject,path.join(repo,'tools/lib/inputhost_runtime_pool.mjs'),path.join(repo,'tools/ymcc_sim/global/inputhost-protocol/provenance.mjs')].map(file=>({file,bytes:fs.readFileSync(file)}));
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const managedInputs=[path.join(repo,'tools/ymcc_sim/global/controller-signal-evidence/environment.mjs'),path.join(repo,'tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json')].map(file=>({file,bytes:fs.readFileSync(file)}));
const managedSpec=JSON.parse(managedInputs[1].bytes.toString('utf8').replace(/^﻿/,''));
if(managedSpec.sdkVersion!=='10.0.400'||managedSpec.runtimeVersion!=='10.0.11'||!path.isAbsolute(managedSpec.dotnetRoot))throw Error('Fixed managed toolchain declaration changed');
const managedDotnet=path.join(managedSpec.dotnetRoot,'dotnet.exe');
const managedEnv=managedFixtureEnvironment({ambient:process.env,dotnetRoot:managedSpec.dotnetRoot,out});
const writtenFiles=[];
const pin=file=>{const bytes=fs.readFileSync(file);return{path:file,bytes:bytes.length,sha256:sha(bytes)};};
function write(name,bytes){const file=path.join(out,name);fs.writeFileSync(file,bytes,{flag:'wx'});writtenFiles.push(pin(file));}
let managedEnvironment;
  for(const name of ['USERPROFILE','DOTNET_CLI_HOME','APPDATA','LOCALAPPDATA','TEMP','NUGET_PACKAGES','MSBuildUserExtensionsPath']){
   const folder=managedEnv[name],relative=path.relative(out,folder);if(path.isAbsolute(relative)||relative==='..'||relative.startsWith('..'+path.sep))throw Error('Managed private directory escaped output');fs.mkdirSync(folder,{recursive:true});
  }
  write('global.json',JSON.stringify({sdk:{version:managedSpec.sdkVersion,rollForward:'disable',allowPrerelease:false}}));
  write('Directory.Build.props','<Project><PropertyGroup><UseSharedCompilation>false</UseSharedCompilation><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>');
  write('Directory.Build.targets','<Project />');
  const versions=relative=>fs.readdirSync(path.join(managedSpec.dotnetRoot,relative),{withFileTypes:true}).filter(e=>e.isDirectory()).map(e=>e.name).sort();
  const variables=Object.fromEntries(Object.entries(managedEnv).sort(([a],[b])=>a.localeCompare(b)));
  const platform={platform:process.platform,arch:process.arch,release:os.release(),version:os.version()};
  const normalized=Object.fromEntries(Object.entries(variables).map(([name,value])=>[name,value.replaceAll(path.resolve(out),'{fixtureRoot}')]));
  managedEnvironment={schemaVersion:1,mode:'fixed-owned-managed-v1',variables,platform,normalizedIdentity:sha(JSON.stringify({variables:normalized,platform})),selectors:['global.json','Directory.Build.props','Directory.Build.targets'].map(name=>pin(path.join(out,name))),declaredSdk:managedSpec.sdkVersion,declaredRuntime:managedSpec.runtimeVersion,resolverMembership:{sdk:versions('sdk'),framework:versions('shared/Microsoft.NETCore.App'),fxr:versions('host/fxr')},hostfxr:pin(path.join(managedSpec.dotnetRoot,'host/fxr',managedSpec.runtimeVersion,'hostfxr.dll')),qualificationScope:'Fixed child environment/selectors and declared component membership; no dynamic loaded-module claim'};

const text=inputs[0].bytes.toString('utf8');const fault=option('--fault','none');const pieces=['private static async Task<HostTerminalCleanup.LoopResult> RunPipeClientAsync(','private sealed class MessageCodec'].map(marker=>({...extract(text,marker),marker}));
write('Extracted.cs','using System.Diagnostics; using System.IO.Pipes; using System.Text; using YeManInputHost.Diagnostics;\ninternal static partial class Program { private const int MaxMessageBytes=16384;\n'+pieces.map(p=>fault==='ignore-parent-cancellation'?p.text.replaceAll('parentLifetime.ExitToken','CancellationToken.None'):p.text).join('\n')+'\n}\n');
let terminalSource=inputs.find(i=>i.file===path.join(host,'HostTerminalCleanup.cs')).bytes.toString('utf8');
for(const [name,before,after] of [['ignore-terminal-deadline','_deadline.Change(DeadlineMilliseconds, Timeout.Infinite);','; // semantic control: deadline deliberately omitted'],['return-without-process-exit','Environment.Exit(exitCode);','return; // semantic control: foreground thread retains process']]){
 if(fault!==name)continue;
 if(terminalSource.split(before).length!==2)throw Error('Nonunique terminal mutation anchor');
 terminalSource=terminalSource.replace(before,after);
}
write('HostTerminalCleanup.cs',terminalSource);write('ParentProcessLifetime.cs',inputs[1].bytes);write('Test.cs',inputs[2].bytes);
write('NuGet.Config','<configuration><packageSources><clear /></packageSources></configuration>');
write('Fixture.csproj','<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><UseAppHost>false</UseAppHost>'+runtimePool.projectProperty+'<RuntimeFrameworkVersion>'+managedSpec.runtimeVersion+'</RuntimeFrameworkVersion><RollForward>Disable</RollForward><ProvideCommandLineArgs>true</ProvideCommandLineArgs><NuGetAudit>false</NuGetAudit><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup><Import Project="FixtureInputs.targets" /></Project>');
write('RuntimePool.cs',runtimePoolProbeSource(runtimePool));
   const originalReferenceItem='@(ReferencePath-&gt;';
   if(buildTargets.split(originalReferenceItem).length!==2)throw Error('Original compiler recording reference item anchor drifted');
   const propertyNames=['NETCoreSdkVersion','RuntimeFrameworkVersion','RollForward','MSBuildSDKsPath','MSBuildToolsPath','RoslynTargetsPath','CscToolPath','CscToolExe','ProvideCommandLineArgs','SkipCompilerExecution','MSBuildEnableWorkloadResolver','CompilerResponseFile','Win32Manifest','Win32Resource','ApplicationIcon','SourceLink','AppConfigForCompiler','ResolvedCodeAnalysisRuleSet','DirectoryBuildPropsPath','DirectoryBuildTargetsPath'];
   const extra='<WriteLinesToFile File="$(MSBuildProjectDirectory)/compiler-commandline-$(Configuration).txt" Lines="@(CscCommandLineArgs)" Overwrite="true" Encoding="UTF-8" /><WriteLinesToFile File="$(MSBuildProjectDirectory)/compiler-properties-$(Configuration).txt" Lines="'+propertyNames.map(name=>name+'&#x9;$('+name+')').join(';')+'" Overwrite="true" Encoding="UTF-8" />';
   write('FixtureInputs.targets',buildTargets.replace(originalReferenceItem,'@(ReferencePathWithRefAssemblies-&gt;').replace('</Target>',extra+'</Target>'));
const localNuGet=path.join(out,'NuGet.Config');
const selectedCase=option('--case',null);
const selectedCases=selectedCase===null?null:selectedCase.split(',');
const configs=option('--configs','Release,Debug').split(',');
const commands=[];let profiles=[];
for(const config of configs){
 for(const [name,argv] of [['build',['build','Fixture.csproj','-c',config,'--nologo','-v:q','-p:UseSharedCompilation=false','-nodeReuse:false','-noAutoResponse','-p:RestoreConfigFile='+localNuGet]],['tests',[path.join(out,'bin',config,'net10.0/Fixture.dll'),...(selectedCase?['--case',selectedCase]:[])]]]){
 const startedAtUtc=new Date().toISOString(),start=performance.now();
 const requestedExecutableSha256=sha(fs.readFileSync(managedDotnet));
 const r=spawnSync(managedDotnet,argv,{cwd:out,env:managedEnv,encoding:'utf8',timeout:60000,windowsHide:true}),elapsedMs=performance.now()-start;
 const logPath=path.join(out,config+'-'+name+'.log'),logText=(r.stdout??'')+(r.stderr??'');write(config+'-'+name+'.log',logText);
 const record={name,config,command:managedDotnet,argv,cwd:out,toolchain:{requestedExecutable:managedDotnet,requestedExecutableSha256,executableIdentityScope:'Requested absolute file before launch; loaded-module closure is not observed',declaredSdk:managedSpec.sdkVersion,environmentMode:'fixed-owned-managed-v1',environmentQualified:true,environmentQualificationScope:'Constructed child variables and owned profile paths only; full dynamic loader is not observed'},pid:r.pid??null,exit:r.status,signal:r.signal,error:r.error?.message??null,errorCode:r.error?.code??null,timedOut:r.error?.code==='ETIMEDOUT',timeoutMs:60000,elapsedMs,startedAtUtc,finishedAtUtc:new Date().toISOString(),log:{path:logPath,bytes:Buffer.byteLength(logText),sha256:sha(Buffer.from(logText))}};
 commands.push(record);write(config+'-'+name+'.process.json',JSON.stringify(record,null,2)+'\n');
 if(name==='build'&&r.status!==0)throw Error('Build failed: '+path.join(out,config+'-'+name+'.log'));
 if(name==='tests'){let result;try{result=JSON.parse(r.stdout.trim().split(/\r?\n/).at(-1));}catch{} profiles.push({config,exit:r.status,result});}
 }
}
const sourceStable=[...inputs,...managedInputs].every(i=>sha(fs.readFileSync(i.file))===sha(i.bytes));
const compilerSummary=recordEvaluatedInputs(out,{artifacts:managedSpec.pinned},{minimumLedgers:configs.length});
const compilerInputs={status:'RECORDED',...compilerSummary,manifest:pin(path.join(out,'evaluated-compiler-inputs.json')),ledgerFiles:configs.map(config=>pin(path.join(out,'evaluated-inputs-'+config+'.txt'))),commandLines:configs.map(config=>pin(path.join(out,'compiler-commandline-'+config+'.txt'))),properties:configs.map(config=>pin(path.join(out,'compiler-properties-'+config+'.txt'))),referenceItem:'ReferencePathWithRefAssemblies',scope:'Original AfterCoreCompile recording target; actual Csc source/reference items and Analyzer members plus compiler arguments/properties, no product execution or loaded-module proof'};
// Snapshot the complete output tree, including compiler/runtime/profile writes.
// result.json is the sole later write and cannot recursively pin itself.
function generatedPaths(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(entry=>{const file=path.join(dir,entry.name),stat=fs.lstatSync(file);if(stat.isSymbolicLink())throw Error('Generated output link refused: '+file);if(stat.isDirectory())return generatedPaths(file);if(!stat.isFile())throw Error('Generated output not regular: '+file);return[file];}).sort();}
const generatedFiles=generatedPaths(out).map(pin);
const generatedStable=writtenFiles.every(row=>{const actual=pin(row.path);return actual.bytes===row.bytes&&actual.sha256===row.sha256;})&&JSON.stringify(generatedFiles)===JSON.stringify(generatedPaths(out).map(pin));
const actualRuntimePool=readRuntimePoolReceipts(out,runtimePool);
const result={selectedCase,selectedCases,configs,runtimePool,actualRuntimePool,fault,status:sourceStable&&generatedStable&&profiles.every(p=>p.exit===0&&p.result?.passed===true&&p.result?.selectedCase===selectedCase&&JSON.stringify(p.result?.selectedCases??null)===JSON.stringify(selectedCases)&&JSON.stringify(p.result?.cases?.map(c=>c.name))===JSON.stringify(selectedCases??defaultCases))?'PASS':'FAIL',sourceStable,generatedStable,generatedFiles,managedEnvironment,compilerInputs,managedSourceInputs:managedInputs.map(i=>({path:i.file,bytes:i.bytes.length,sha256:sha(i.bytes)})),profiles,commands,processes:commands,sources:inputs.map(i=>({path:i.file,sha256:sha(i.bytes)})),ranges:pieces.map(({text,...r})=>({...r,sha256:sha(text)})),productEntryExecuted:false,hardwareIo:false,sdkInputAssembliesLoaded:false,scope:'Exact production pipe loop/codec/parent lifetime and terminal cleanup. Existing default10 unchanged; explicit terminal cases execute ProcessExit on inert owned child processes, held-file release and original10s deadline. SDK and HostSession are ports; production Main/startup and real driver/device release are not executed.'};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({status:result.status,profiles:profiles.map(p=>({config:p.config,exit:p.exit,cases:p.result?.cases})),sourceStable}));if(result.status!=='PASS')process.exitCode=1;
}
if(process.argv[1]&&fs.existsSync(process.argv[1])&&fs.realpathSync.native(process.argv[1])===SELF){
 try{execute(process.argv.slice(2));}catch(error){console.error(error.stack??String(error));process.exitCode=1;}
}
