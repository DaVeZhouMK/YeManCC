// Run the original lifecycle assertions with only two explicit terminal bindings.
// The unmodified original entry's build failure remains a separate retained result.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {createOfflineHostFixture} from './msi_steamdeck_release_fence_1726_selftest.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),args=process.argv.slice(2);
const option=(key,d)=>{const at=args.indexOf(key);return at<0?d:args[at+1];};
const out=path.resolve(option('--out','G:/YeManCC-Work/Testing/YMCC/Evidence/VC-HostManaged23/legacy-adapted-001'));
const source=path.resolve(option('--inputs','G:/YeManCC-Work/Testing/YMCC/Evidence/VC-HostManaged23/current-005/sources'));
if(fs.existsSync(out))throw Error('Fresh output required');fs.mkdirSync(out,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),write=(n,v)=>fs.writeFileSync(path.join(out,n),typeof v==='string'?v:JSON.stringify(v,null,2)+'\n');
const tracked=[];
function read(relative){const file=path.join(source,relative),data=fs.readFileSync(file);tracked.push({file,relative,sha256:sha(data)});return data.toString('utf8');}
const host=path.join(source,'InputHost'),generated=createOfflineHostFixture(out,host,true,{includePsOwnershipSupport:true,gameFeedbackCatalogPath:path.join(source,'capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs')});
write('Extracted.cs',generated.fake);
write('Legacy.cs',read('tools/msi_steamdeck_release_fence_1726_selftest.cs').replace('internal static class Fixture','internal static partial class Fixture').replace('private static int Main()','private static int RunLegacyFenceTests()').replace('Fixture.Reset(); body(); ++_tests;','Fixture.Reset(); if (ContinueLegacy) LegacyRecord(name, body); else body(); ++_tests;'));
write('Test.cs',read('tools/inputhost_lifecycle_selftest.cs'));
write('Boundary.cs','namespace YeManInputHost { internal sealed class UsbipMaintenanceGate:System.IDisposable { internal static UsbipMaintenanceGate? TryEnter(bool required)=>new(); public void Dispose(){} } }\ninternal static partial class Program { private sealed partial class HostSession { private void ResetVirtualSignalEvidenceSafely(){} } }\n');
for(const name of ['MsiSteamDeckReleaseFence1726.cs','MsiVirtualReportChain1410.cs','CanonicalFrameFingerprintMemo.cs','HostResponseFields.cs'])write(name,read('InputHost/'+name));
write('Entry.cs','internal static partial class Program { private static int Main(string[] args)=>args.Length==1?RunLegacyFenceTests():LifecycleMain(RunLegacyFenceTests); }');
write('Fixture.csproj','<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><NuGetAudit>false</NuGetAudit><EnableNETAnalyzers>false</EnableNETAnalyzers><UseAppHost>false</UseAppHost></PropertyGroup></Project>');
write('NuGet.Config','<configuration><packageSources><clear /></packageSources></configuration>');
const commands=[],profiles=[];
for(const config of ['Release','Debug']){
 for(const [name,argv]of [['build',['build','Fixture.csproj','-c',config,'--nologo','-v:q']],['original-legacy-tests',[path.join(out,'bin',config,'net10.0/Fixture.dll'),'--legacy-original']],['current-tests',[path.join(out,'bin',config,'net10.0/Fixture.dll')]]]){
  const r=spawnSync('dotnet',argv,{cwd:out,encoding:'utf8',windowsHide:true,timeout:60000});write(config+'-'+name+'.log',(r.stdout??'')+(r.stderr??''));commands.push({config,name,argv,status:r.status,error:r.error?.message??null});write('commands.json',commands);
  if(name==='build'&&r.status!==0)throw Error('Legacy-adapted build failed');
  if(name==='original-legacy-tests'&&r.status!==1)throw Error('Original legacy assertion must still fail');
  if(name==='current-tests'){let result;try{result=JSON.parse(r.stdout.trim().split(/\r?\n/).at(-1));}catch{}profiles.push({config,exit:r.status,result});}
 }
}
const currentSourceStable=generated.receipt.every(r=>r.file.includes('/sources/')||r.file.includes('\\sources\\'))&&tracked.every(r=>sha(fs.readFileSync(r.file))===r.sha256);
const result={status:currentSourceStable&&profiles.every(p=>p.exit===0)?'PASS':'FAIL',originalUnmodifiedEntry:'BUILD_FAIL_RETAINED_AT_existing-original-001',originalLegacyAssertionExit:1,profiles,currentSourceStable,tracked,extracted:generated.receipt,boundaryAdaptation:['Test-only UsbipMaintenanceGate returns a disposable inert lease; actual USB mutex/service/driver not exercised.','Test-only ResetVirtualSignalEvidenceSafely inert optional observer; actual virtual-signal diagnostics not exercised.'],productionChanged:false};
write('result.json',result);console.log(JSON.stringify({status:result.status,profiles:profiles.map(p=>({config:p.config,exit:p.exit,originalLegacyFailures:p.result?.legacyFailures,cases:p.result?.cases.length}))}));if(result.status!=='PASS')process.exitCode=1;
