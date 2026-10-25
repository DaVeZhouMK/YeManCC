import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const toolchainPath=path.join(repo,'tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json');
const toolchain=JSON.parse(fs.readFileSync(toolchainPath,'utf8').replace(/^\uFEFF/,''));
const dotnet=path.join(toolchain.dotnetRoot,'dotnet.exe');
if(!path.isAbsolute(dotnet)||!fs.existsSync(dotnet))throw Error('Reviewed dotnet toolchain unavailable');
const args=process.argv.slice(2);
if(args.length!==2||args[0]!=='--out'||!path.isAbsolute(args[1]))throw Error('Use --out ABS_FRESH_PRIVATE_DIRECTORY');
const out=path.resolve(args[1]);
if(fs.existsSync(out)||out.toLowerCase().startsWith(repo.toLowerCase()+path.sep))throw Error('Fresh private output outside repository required');
fs.mkdirSync(out,{recursive:true});
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const sourcePath=path.join(repo,'InputHost/Program.cs');
const source=fs.readFileSync(sourcePath,'utf8');
const fixture=path.join(repo,'tools/fixtures/usbip_backend_preparation/Fixture.cs');
const fixtureBytes=fs.readFileSync(fixture);
const mask=text=>text.replace(/"(?:\\.|[^"\\])*"|\/\/[^\n]*|\/\*[\s\S]*?\*\//g,m=>m.replace(/[^\n]/g,' '));
function member(marker){
 const start=source.indexOf(marker);
 if(start<0||source.indexOf(marker,start+marker.length)>=0)throw Error('Nonunique actual consumer '+marker);
 const masked=mask(source);let open=masked.indexOf('{',start),depth=0;
 for(let i=open;i<masked.length;i++){if(masked[i]==='{')depth++;else if(masked[i]==='}'&&--depth===0)return source.slice(start,i+1);}
 throw Error('Unclosed production member');
}
const originalMembers=[member('private static T CreateTargetWithBackendWarning<T>'),member('private static bool TryEnsureDriverInstalled('),member('private static bool TryEnsureUsbipBackend(')].join('\n');
const wrapper=members=>'using YeManInputHost.Diagnostics;\ninternal static partial class ProgramFixture {\n'+members+'\ninternal static int Execute(bool usbip,UsbipSetupEvidence? evidence,Func<int> factory,out string receipt) => CreateTargetWithBackendWarning(()=>{var ready=TryEnsureDriverInstalled(new HMContext(),out var detail,usbip,evidence);return(ready,detail);},factory,out receipt);\n}';
const bindings=[{path:toolchainPath,sha256:hash(fs.readFileSync(toolchainPath))},{path:sourcePath,sha256:hash(fs.readFileSync(sourcePath))},{path:fixture,sha256:hash(fixtureBytes)},{path:fileURLToPath(import.meta.url),sha256:hash(fs.readFileSync(fileURLToPath(import.meta.url)))}];
fs.writeFileSync(path.join(out,'SOURCE-BINDINGS.json'),JSON.stringify({bindings,selectedMembersSha256:hash(originalMembers),normalHostStarted:false,sdkAndDeviceTerminals:'inert HMContext and evidence callbacks only'},null,2));
const results=[];
for(const test of [
 {name:'positive',members:originalMembers},
 {name:'negative-drop-progress',from:'HMContext.InstallUsbipBackend(progress);',to:'HMContext.InstallUsbipBackend(null);',expected:'progress-callback-reaches-official-install-only'},
 {name:'negative-backend-warning-gates-factory',from:'return createTarget();',to:'return backendReceipt.StartsWith("backend-warning:",StringComparison.Ordinal) ? default! : createTarget();',expected:'factory-called-once-even-when-backend-warning'}
]){
 const directory=path.join(out,test.name);fs.mkdirSync(directory);
 let members=test.members??originalMembers;
 if(test.from){if(members.split(test.from).length!==2)throw Error('Mutation anchor drift '+test.name);members=members.replace(test.from,test.to);}
 fs.writeFileSync(path.join(directory,'Consumers.cs'),wrapper(members));
 fs.writeFileSync(path.join(directory,'Fixture.cs'),fixtureBytes);
 fs.writeFileSync(path.join(directory,'Fixture.csproj'),'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>');
 fs.writeFileSync(path.join(directory,'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>');
 for(const [stage,argv] of [['build',['build','Fixture.csproj','-c','Release','--nologo']],['run',[path.join(directory,'bin/Release/net10.0-windows/Fixture.dll'),path.join(directory,'RESULT.json')]]]){
  const r=spawnSync(dotnet,argv,{cwd:directory,encoding:'utf8',windowsHide:true,timeout:60000,env:{...process.env,DOTNET_NOLOGO:'1',DOTNET_CLI_TELEMETRY_OPTOUT:'1'}});
  fs.writeFileSync(path.join(directory,stage+'.log'),(r.stdout??'')+(r.stderr??'')+(r.error?'\n'+String(r.error):''));
  const expected=stage==='run'&&test.expected?1:0;
  if(r.error||r.status!==expected)throw Error(test.name+' '+stage+' failed; inspect '+path.join(directory,stage+'.log'));
 }
 const result=JSON.parse(fs.readFileSync(path.join(directory,'RESULT.json'),'utf8'));
 if(test.expected?!result.failures.some(x=>x.includes(test.expected)):!result.pass)throw Error('Designated assertion not proven '+test.name);
 results.push({name:test.name,result,expectedRejection:!!test.expected});
}
for(const binding of bindings)if(hash(fs.readFileSync(binding.path))!==binding.sha256)throw Error('Source drift '+binding.path);
const result={pass:true,results,sourcesStillCurrent:true,realSdkLoaded:false,realHostStarted:false,driverOperations:false};
fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result));
