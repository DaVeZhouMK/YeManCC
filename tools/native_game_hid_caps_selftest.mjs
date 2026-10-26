import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2);
const option=(name,fallback)=>{const index=args.indexOf(name);return index<0?fallback:args[index+1];};
const output=path.resolve(option('--output','G:/YeManCC-Work/Build/Validation/NativeGameHidCaps-20261018'));
const baseline=option('--baseline',null);
const sourcePath=path.resolve(option('--source',path.join(repo,'InputHost/MsiNativePortCycle.cs')));
const hash=value=>crypto.createHash('sha256').update(value).digest('hex');
const original=fs.readFileSync(sourcePath);
// The sole new import has a fail-closed fixture body; unknown extra imports still fail admission.
const cancellationWaitImport='    [DllImport("kernel32.dll",EntryPoint="WaitForSingleObject",SetLastError=true)] private static extern uint WaitForCycleCancellation(SafeWaitHandle handle,uint milliseconds);';
const cancellationWaitInert=original.toString('utf8').split(/\r?\n/).includes(cancellationWaitImport);
fs.mkdirSync(output,{recursive:true});
const fixturePath=path.join(repo,'tools/fixtures/native_game_hid_caps/Fixture.cs');
const summaries=[];
const fixtureSha256=hash(fs.readFileSync(fixturePath));
const runnerSha256=hash(fs.readFileSync(fileURLToPath(import.meta.url)));
const identity={startedAt:new Date().toISOString(),sourcePath,sourceSha256:hash(original),fixtureSha256,runnerSha256};
fs.writeFileSync(path.join(output,'SOURCE.cs'),original);
fs.writeFileSync(path.join(output,'RESULT.json'),JSON.stringify({...identity,passed:false,status:'started',configurations:[]},null,2)+'\n');
process.on('uncaughtExceptionMonitor',error=>fs.writeFileSync(path.join(output,'RESULT.json'),JSON.stringify({...identity,passed:false,status:'failed',error:String(error.message).slice(0,2000),configurations:summaries},null,2)+'\n'));
for(const configuration of ['Release','Checked']){
 const directory=path.join(output,configuration);fs.mkdirSync(directory,{recursive:true});
 let source=original.toString('utf8').replace('internal static class MsiNativePortCycle','internal static partial class MsiNativePortCycle').replace('using Microsoft.Win32;','using Registry = InertRegistry.Registry;');
 const imports=[...source.matchAll(/^    \[DllImport[^\r\n]+$/gm)].map(match=>match[0]);
 assert.equal(imports.length,cancellationWaitInert?16:15);
 for(const declaration of imports)source=source.replace(declaration,'');
 assert(!source.includes('DllImport'));
 fs.writeFileSync(path.join(directory,'Actual.cs'),source);fs.copyFileSync(fixturePath,path.join(directory,'Fixture.cs'));
 const project=path.join(directory,'Fixture.csproj');
 fs.writeFileSync(project,`<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors><CheckForOverflowUnderflow>${configuration==='Checked'}</CheckForOverflowUnderflow><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>`);
 for(const [label,command] of [['build',['build',project,'--configuration','Release','--nologo','--verbosity','quiet']],['run',[path.join(directory,'bin/Release/net10.0/Fixture.dll'),path.join(directory,'RESULT.json')]]]){
  const result=spawnSync('dotnet',command,{cwd:directory,encoding:'utf8',timeout:120000});fs.writeFileSync(path.join(directory,label+'.log'),(result.stdout??'')+(result.stderr??''));assert.equal(result.status,0,configuration+' '+label+' '+result.stdout+' '+result.stderr);
 }
 const result=JSON.parse(fs.readFileSync(path.join(directory,'RESULT.json'),'utf8'));assert(result.passed);summaries.push({configuration,...result,results:undefined});console.log(configuration+': '+result.cases+' cases / '+result.assertions+' assertions PASS');
}
if(baseline){
 const directory=path.join(output,'baseline');fs.mkdirSync(directory,{recursive:true});
 let source=fs.readFileSync(baseline,'utf8').replace('internal static class MsiNativePortCycle','internal static partial class MsiNativePortCycle').replace('using Microsoft.Win32;','using Registry = InertRegistry.Registry;');
 source=source.replace(/^    \[DllImport[^\r\n]+$/gm,'');
 const definition=original.toString('utf8').replace(/\r\n/g,'\n');
 const structs=definition.slice(definition.indexOf('    [StructLayout(LayoutKind.Sequential)]\n    private struct HidAttributes'),definition.indexOf('    private static bool HidInterfaceMatches'));
 assert(structs.includes('private struct HidCaps'));
 source+='\ninternal static partial class MsiNativePortCycle {\n'+structs+'\n}\n';
 fs.writeFileSync(path.join(directory,'Actual.cs'),source);fs.copyFileSync(fixturePath,path.join(directory,'Fixture.cs'));fs.copyFileSync(path.join(output,'Release/Fixture.csproj'),path.join(directory,'Fixture.csproj'));
 for(const [label,command] of [['build',['build',path.join(directory,'Fixture.csproj'),'--configuration','Release','--nologo','--verbosity','quiet']],['run',[path.join(directory,'bin/Release/net10.0/Fixture.dll'),path.join(directory,'RESULT.json'),'--baseline']]]){
  const result=spawnSync('dotnet',command,{cwd:directory,encoding:'utf8',timeout:120000});fs.writeFileSync(path.join(directory,label+'.log'),(result.stdout??'')+(result.stderr??''));assert.equal(result.status,0,'baseline '+label+' '+result.stdout+' '+result.stderr);
 }
 const result=JSON.parse(fs.readFileSync(path.join(directory,'RESULT.json'),'utf8'));assert.equal(result.results[0].result,8);summaries.push({configuration:'original-empty-compatible-counterexample',...result,results:undefined});
}
const mutations=[
 {name:'bypass-rog-hid-mutation-quarantine',before:'if(!GameNodeRefreshMutationAllowed(root,target))',after:'if(false && !GameNodeRefreshMutationAllowed(root,target))',required:'quarantined ROG HID node has no device operations',expected:''},
 {name:'wash-failed-execution-success',before:'return enabled;',after:'return true;',required:'result=0 expected=6',expected:''},
 {name:'skip-owned-enable-compensation',before:'if(!enabled && compensationCurrent is not null)',after:'if(false && !enabled && compensationCurrent is not null)',required:'bounded exact enable compensation',expected:''},
 {name:'skip-return-identity-revalidation',before:'var currentReturn=compensationCurrent();',after:'var currentReturn=true;',required:'no retry on changed or unknown identity',expected:''},
 {name:'suppress-refresh-failure-facts',before:'refreshFacts?.WriteFailure(error);return 6;',after:'_ = error;return 6;',required:'refresh diagnostic present',expected:''},
 {name:'original-usb-only-xusb',before:'(root.StartsWith(@"USB\\VID_0B05&",StringComparison.OrdinalIgnoreCase) && instance.StartsWith(@"HID\\",StringComparison.OrdinalIgnoreCase))',after:'false',required:'field-hid-xusb',expected:'result=2 expected=0'},
 {name:'trust-without-live-caps',before:'currentUsage is not null?currentUsage()',after:'currentUsage is not null?true',required:'non-game'},
 {name:'skip-pre-mutation-authority',before:'if(currentAuthority is not null && !currentAuthority())throw new HidAuthorityChangedException();',after:'if(currentAuthority is not null && ((Func<bool>)(()=>false))())throw new HidAuthorityChangedException();',required:'immediate-before-disable'},
];
for(const mutation of mutations){
 const directory=path.join(output,mutation.name);fs.mkdirSync(directory,{recursive:true});
 const actual=fs.readFileSync(path.join(output,'Release/Actual.cs'),'utf8');assert.equal(actual.split(mutation.before).length,2);
 fs.writeFileSync(path.join(directory,'Actual.cs'),actual.replace(mutation.before,mutation.after));fs.copyFileSync(fixturePath,path.join(directory,'Fixture.cs'));fs.copyFileSync(path.join(output,'Release/Fixture.csproj'),path.join(directory,'Fixture.csproj'));
 const build=spawnSync('dotnet',['build',path.join(directory,'Fixture.csproj'),'--configuration','Release','--nologo','--verbosity','quiet'],{cwd:directory,encoding:'utf8',timeout:120000});fs.writeFileSync(path.join(directory,'build.log'),(build.stdout??'')+(build.stderr??''));assert.equal(build.status,0,mutation.name+' build '+build.stdout+build.stderr);
 const run=spawnSync('dotnet',[path.join(directory,'bin/Release/net10.0/Fixture.dll'),path.join(directory,'RESULT.json')],{cwd:directory,encoding:'utf8',timeout:60000});fs.writeFileSync(path.join(directory,'run.log'),(run.stdout??'')+(run.stderr??''));assert.equal(run.status,1,mutation.name+' must be caught');
 const result=JSON.parse(fs.readFileSync(path.join(directory,'RESULT.json'),'utf8'));assert(result.failures.some(failure=>failure.includes(mutation.required)&&failure.includes(mutation.expected??'result=0 expected=8')));summaries.push({configuration:mutation.name,expectedFailure:true,detectedFailures:result.failures.length,cases:result.cases,assertions:result.assertions});console.log(mutation.name+': detected '+result.failures.length+' failures');
}
assert.equal(hash(fs.readFileSync(sourcePath)),hash(original));
assert.equal(hash(fs.readFileSync(fixturePath)),fixtureSha256);
assert.equal(hash(fs.readFileSync(fileURLToPath(import.meta.url))),runnerSha256);
fs.writeFileSync(path.join(output,'RESULT.json'),JSON.stringify({...identity,passed:true,status:'passed',configurations:summaries,realHardwareAccess:false,realSdkLoaded:false,productMainExecuted:false,fullActualHelperCompiled:true,replacements:['CM/HID/file imports replaced by inert bodies','registry and parent lifetime inert','Nefarius classes inert','Stopwatch forwards real monotonic time except explicit inert timing cases','partial keyword only','optional cancellation wait fails closed without OS access'],importCount:cancellationWaitInert?16:15,cancellationWaitInert},null,2)+'\n');
