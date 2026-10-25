// Private net10 fixture: SDK read as PE bytes and decompiled; never loaded or
// referenced by the test executable. All workers, CTS and teardown effects fake.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const self=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(self),'..');
const workspace=path.resolve(repo,'../../..');
const current=path.join(workspace,'Build/Validation/VirtualController-Lifecycle-20261021-G3E-XboxSwitch');
const allowed=[current,path.join(workspace,'Build/Validation/VirtualInput-BugPriority-R103-20261016/output-stop-helper-validation'),path.join(workspace,'Build/Validation/VirtualController-Lifecycle-20261019/host')];
const out=path.resolve(process.argv[2]??path.join(current,'field170350-backup1018/official-upstream/post112-sidecar/output-stop-validation'));
if(!allowed.some(root=>{const r=path.relative(root,out);return !r.startsWith('..')&&!path.isAbsolute(r);}))throw Error('Output outside authorized lifecycle evidence.');
fs.mkdirSync(out,{recursive:true});
const run=path.join(out,`run-${new Date().toISOString().replace(/[:.]/g,'')}-${process.pid}`);fs.mkdirSync(run);
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const fixture=path.join(repo,'tools/fixtures/hidmaestro/2.0.0');
const bindingFile=path.join(fixture,'binary-binding.json');
const binding=JSON.parse(fs.readFileSync(bindingFile,'utf8'));
if(binding.sdkBytes!==63076352||binding.sdkSha256!=='8fe7bdb21ec497591f527b7b87c4bc7b41b45c35a06cc211a7a0dce48838ed2c'||binding.mvid!=='86aed9a3-c024-4596-b02c-650082ffb852')throw Error('Unrecognized SDK binding.');
const sdk=path.join(repo,'deps/hidmaestro/HIDMaestro.Core.dll');
function read(source){const before=fs.statSync(source),data=fs.readFileSync(source),after=fs.statSync(source);if(before.size!==after.size||before.mtimeMs!==after.mtimeMs)throw Error('Input changed: '+source);return {source,data,bytes:data.length,sha256:hash(data)};}
const sourcePaths=['InputHost/HostControllerOutputStop.cs','InputHost/HostControllerIdleCtsRelease.cs','tools/host_controller_output_stop_selftest.cs'].map(p=>path.join(repo,p)).concat([self,bindingFile],Object.keys(binding.members).map(f=>path.join(fixture,f)));
const sources=sourcePaths.map(read),sdkInput=read(sdk);
if(sdkInput.bytes!==binding.sdkBytes||sdkInput.sha256!==binding.sdkSha256)throw Error('Pinned release DLL mismatch.');
const json=(name,value)=>fs.writeFileSync(path.join(run,name),JSON.stringify(value,null,2)+'\n');
json('source-bindings-before.json',sources.map(({data,...r})=>r));
const commands=[];
function execute(name,exe,args){
 const result=spawnSync(exe,args,{cwd:run,encoding:'utf8',timeout:60000,windowsHide:true,maxBuffer:16*1024*1024,
 env:{...process.env,DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_NOLOGO:'1',DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE:'true'}});
 fs.writeFileSync(path.join(run,name+'.stdout.log'),result.stdout??'');fs.writeFileSync(path.join(run,name+'.stderr.log'),result.stderr??'');
 commands.push({name,exe,args,status:result.status,error:result.error?.message??null});json('commands.json',commands);
 if(result.error)throw Error(`${name}: ${result.error.message}`);
 return result;
}
const dotnet=process.env.DOTNET_EXE??'dotnet';
function dot(name,args){const r=execute(name,dotnet,args);if(r.status!==0)throw Error(`${name} failed: ${path.join(run,name+'.stdout.log')}`);return r.stdout;}
function extract(text,signature){text=text.replace(/\r\n/g,'\n');const start=text.indexOf('\t'+signature+'\n');if(start<0)throw Error('Decompile signature missing: '+signature);const open=text.indexOf('{',start);let level=1,end=open+1;for(;level&&end<text.length;end++){if(text[end]==='{')level++;else if(text[end]==='}')level--;}return text.slice(start,end)+'\n';}
let results,failure;const negatives=[];
try{
 const ilspy=process.env.ILSPYCMD_EXE??path.join(os.homedir(),'.dotnet/tools/ilspycmd.exe');
 const controller=execute('actual-sdk-controller-decompile',ilspy,['-t','HIDMaestro.HMController',sdk]);
 const clock=execute('actual-sdk-clock-decompile',ilspy,['-t','HIDMaestro.Internal.DeviceClock`1',sdk]);
 if(controller.status!==0||clock.status!==0)throw Error('Actual release decompile failed.');
 const methods={};
 for(const [file,entry] of Object.entries(binding.members)){
  const actual=extract(file.startsWith('HMController.')?controller.stdout:clock.stdout,entry.signature);
  const committed=fs.readFileSync(path.join(fixture,file),'utf8');
  if(actual!==committed||hash(actual)!==entry.sha256)throw Error('Exact release method fixture mismatch: '+file);
  methods[file]=actual;
 }
 json('pinned-sdk-binding.json',{sdk:(({data,...r})=>r)(sdkInput),binding,actualReleaseDecompiled:true,fixtureMethodsVerified:Object.keys(methods),targetAssemblyLoadedByTest:false});
 fs.writeFileSync(path.join(run,'HostControllerOutputStop.cs'),sources[0].data);
 fs.writeFileSync(path.join(run,'HostControllerIdleCtsRelease.cs'),sources[1].data);
 fs.writeFileSync(path.join(run,'SelfTest.cs'),sources[2].data);
 const controllerMethods=Object.entries(methods).filter(([n])=>n.startsWith('HMController.')).map(([,s])=>s).join('\n');
 const clockMethods=Object.entries(methods).filter(([n])=>n.startsWith('DeviceClock.')&&binding.members[n].compile!==false).map(([,s])=>s).join('\n');
 fs.writeFileSync(path.join(run,'ExactDispose.fixture.cs'),`#nullable enable annotations\n#nullable disable warnings\nusing System;\nusing System.Threading;\nnamespace HIDMaestro { using Thread = OutputStopFixture.FakeReader; internal sealed partial class HMController {\n${controllerMethods}\n} }\nnamespace HIDMaestro.Internal { internal sealed partial class DeviceClock<TTick> {\n${clockMethods}\n} }\n`);
 fs.writeFileSync(path.join(run,'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>\n');
 const project=path.join(run,'OutputStopSelfTest.csproj');
 fs.writeFileSync(project,`<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>
<OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>disable</ImplicitUsings><Nullable>enable</Nullable>
<TreatWarningsAsErrors>true</TreatWarningsAsErrors><EnableDefaultCompileItems>false</EnableDefaultCompileItems><UseAppHost>false</UseAppHost>
<Deterministic>true</Deterministic><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit><RestoreSources></RestoreSources>
<AssemblyName>HostControllerOutputStopSelfTest</AssemblyName></PropertyGroup><ItemGroup>
<Compile Include="HostControllerOutputStop.cs"/><Compile Include="HostControllerIdleCtsRelease.cs"/><Compile Include="SelfTest.cs"/><Compile Include="ExactDispose.fixture.cs"/>
</ItemGroup></Project>\n`);
 if(!dot('sdk-version',['--version']).trim().startsWith('10.'))throw Error('net10 SDK required.');
 dot('restore',['restore',project,'--configfile',path.join(run,'NuGet.Config'),'--nologo','--verbosity','minimal']);
 dot('build',['build',project,'--configuration','Release','--no-restore','--nologo','--verbosity','minimal']);
 const executable=path.join(run,'bin/Release/net10.0/HostControllerOutputStopSelfTest.dll');
 results=JSON.parse(dot('selftest',[executable,sdk]));json('selftest-results.json',results);
 if(results.failed!==0||!results.pureFakeOperations||results.targetAssemblyLoaded||results.realThreadsStarted||results.devicesOrDriversInvoked)throw Error('Unexpected fixture result.');
 const mutations=[
  {name:'cancel-closed-source',file:'HostControllerOutputStop.cs',from:'if (!closed)',to:'if (true)'},
  {name:'ignore-output-join-timeout',file:'HostControllerOutputStop.cs',from:'if (!runtime.Join(reader, remaining))',to:'if (!runtime.Join(reader, remaining) && false)'},
  {name:'ignore-idle-thread-termination',file:'HostControllerIdleCtsRelease.cs',from:'if (!Stopped(runtime, thread, unstarted)) return false;',to:'if (!Stopped(runtime, thread, unstarted) && false) return false;'}
 ];
 for(const mutation of mutations){
  const p=path.join(run,mutation.file),original=fs.readFileSync(p,'utf8');if(!original.includes(mutation.from))throw Error('Mutation anchor missing.');
  fs.writeFileSync(p,original.replace(mutation.from,mutation.to));
  try{
   dot(mutation.name+'-build',['build',project,'--configuration','Release','--no-restore','--nologo','--verbosity','minimal']);
   const bad=execute(mutation.name+'-test',dotnet,[executable,sdk]);
   const outcome=JSON.parse(bad.stdout);
   if(bad.status===0||outcome.failed===0)throw Error('Mutation was not detected: '+mutation.name);
   negatives.push({name:mutation.name,compiled:true,failedCases:outcome.cases.filter(c=>c.status==='fail').map(c=>c.name)});
  }finally{fs.writeFileSync(p,original);}
 }
 // Leave the generated executable corresponding to the reviewed sources.
 dot('final-build',['build',project,'--configuration','Release','--no-restore','--nologo','--verbosity','minimal']);
 results=JSON.parse(dot('final-selftest',[executable,sdk]));json('selftest-results.json',results);
 json('negative-controls.json',negatives);
}catch(error){
 failure=String(error?.message??error);
 const p=path.join(run,'selftest.stdout.log');if(fs.existsSync(p)){try{results=JSON.parse(fs.readFileSync(p,'utf8'));json('selftest-results.json',results);}catch{}}
}
const after=sources.concat([sdkInput]).map(({source,sha256})=>{const current=hash(fs.readFileSync(source));return {source,sha256:current,unchanged:current===sha256};});
json('source-bindings-after.json',after);if(!after.every(r=>r.unchanged))failure??='Input changed during validation; rerun required.';
const summary={version:2,status:failure?'fail':'pass',failure:failure??null,runDirectory:run,passed:results?.passed??null,failed:results?.failed??null,
 warningsAsErrors:true,helpersCompiledUnmodified:true,exactReleaseMethodsVerified:!failure,negativeControls:negatives,
 pureFakeOperations:true,realSDKLoadedByTest:false,realHostBuiltOrRun:false,realThreadsStarted:false,devicesOrDriversInvoked:false,
 sourceStable:after.every(r=>r.unchanged),sourceBindings:sources.map(({data,...r})=>r)};
json('validation-summary.json',summary);fs.writeFileSync(path.join(out,'latest-validation.json'),JSON.stringify(summary,null,2)+'\n');
console.log(JSON.stringify(summary,null,2));process.exitCode=failure?1:0;