// Complete helper source; only OS/SDK boundaries are substituted. No InputHost entry is run.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2),arg=(name,fallback)=>{const i=args.indexOf(name);return i<0?fallback:args[i+1];};
const output=path.resolve(arg('--output','G:/YeManCC-Work/Build/Validation/MSI-NativeOff-AuthorityFix-20261018/Helper'));
const baseline=path.resolve(arg('--baseline','G:/YeManCC-Work/Build/Validation/HIDMaestro-112-CPUCompatibility-20261018/HostReplay/source/MsiNativePortCycle.cs'));
const preexisting=path.resolve(arg('--preexisting',path.join(output,'MsiNativePortCycle.before.cs')));
const current=path.join(repo,'InputHost/MsiNativePortCycle.cs');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const write=(p,b)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,b);};
const legacySha='970197d52a60b407bc69a29f1f0d9be51742888cc25af61db6359ea396d15149';
const preexistingSha='cca63e4be5e481e8688433bdfdc6cb71f026de5d484c9b0c73d8d27639b70d84';
const currentOnly=args.includes('--current-only'),preexistingOnly=args.includes('--preexisting-only'),legacyOnly=args.includes('--legacy-only');
const variants=preexistingOnly?['preexisting']:currentOnly?['current']:legacyOnly?['legacy']:['legacy',...(fs.existsSync(preexisting)?['preexisting']:[]),'current'];
const sourceFiles={legacy:baseline,preexisting,current};
const direct={Locate:'ref node,instance,flags',Property:'node,key.Format,key.Id,out type,buffer,ref bytes,flags',Parent:'out parent,node,flags',DeviceId:'node,identifier,length,flags',Disable:'node,flags',Enable:'node,flags',InterfaceSize:'out characters,ref type,instance,flags',Interfaces:'ref type,instance,buffer,characters,flags',InterfaceProperty:'path,key.Format,key.Id,out type,buffer,ref bytes,flags',OpenHid:'path,access,share,security,creation,flags,template',HidAttributesRead:'handle,ref attributes',HidPreparsedRead:'handle,out preparsed',HidCapabilitiesRead:'preparsed,out caps',HidPreparsedFree:'preparsed'};
const aliases={HidAttributesRead:'Attributes',HidPreparsedRead:'PreparsedRead',HidCapabilitiesRead:'CapabilitiesRead',HidPreparsedFree:'PreparsedFree'};
const adapter=new Set(['Locate','Property','InterfaceSize','Interfaces','InterfaceProperty','OpenHid','HidAttributesRead','HidPreparsedRead','HidCapabilitiesRead','HidPreparsedFree']);
function compileAndRun(variant,raw,define='',expectFailure=false){
 const dir=path.join(output,'offline',variant),substitutions=[];
 let source=raw.toString('utf8').replace('using Microsoft.Win32;','using Registry = R114Offline.Registry;');
 source=source.replace(/^    \[DllImport[^\n]+\] (?:\[return:[^\n]+\] )?private static extern ([\w?]+) (\w+)\(([^\n]*)\);\r?$/gm,(whole,ret,name,parameters)=>{
  assert(direct[name],'known I/O declaration: '+name);substitutions.push(name);
  return `    private static ${ret} ${name}(${parameters}) => ${adapter.has(name)?'AuthorityIo':'R114Offline.Boundary'}.${aliases[name]??name}(${direct[name]});`;
 });
 assert(!source.includes('DllImport'),'zero unmanaged entry points');assert.equal(substitutions.length,define==='LEGACY'?8:14,'all OS functions replaced');
 write(path.join(dir,'Helper.cs'),source);
 for(const [dest,src] of [['Fixture.cs','tools/msi_native_hid_authority_selftest.cs'],['AuthorityIo.cs','tools/msi_native_hid_authority_boundaries.cs'],['Boundaries.cs','tools/fixtures/msi_game_authority_diagnostics_r114/Boundaries.cs']])write(path.join(dir,dest),fs.readFileSync(path.join(repo,src)));
 write(path.join(dir,'Offline.csproj'),`<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems><NuGetAudit>false</NuGetAudit><TreatWarningsAsErrors>true</TreatWarningsAsErrors><DefineConstants>${define}</DefineConstants></PropertyGroup><ItemGroup><Compile Include="Helper.cs"/><Compile Include="Fixture.cs"/><Compile Include="AuthorityIo.cs"/><Compile Include="Boundaries.cs"/></ItemGroup></Project>`);
 const built=spawnSync('dotnet',['build',path.join(dir,'Offline.csproj'),'-c','Release','--nologo','-v','minimal'],{encoding:'utf8',timeout:120000});
 write(path.join(dir,'compile.log'),(built.stdout??'')+(built.stderr??''));assert.equal(built.status,0,built.stdout+built.stderr);
 const ran=spawnSync('dotnet',[path.join(dir,'bin/Release/net10.0/Offline.dll'),path.join(dir,'results.json')],{encoding:'utf8',timeout:60000});
 write(path.join(dir,'run.log'),(ran.stdout??'')+(ran.stderr??''));
 if(expectFailure)assert.equal(ran.status,1,variant+' must fail assertions, not crash/compile-fail: '+ran.stdout+ran.stderr);
 else assert.equal(ran.status,0,ran.stdout+ran.stderr);
 const results=JSON.parse(fs.readFileSync(path.join(dir,'results.json'),'utf8'));
 console.log(variant+': '+JSON.stringify({assertions:results.assertions,failures:results.failures,cases:results.cases.length,otherFamilies:results.otherFamilies.length,fullRunDifferential:results.fullRunDifferential.length}));
 return {variant,sha256:sha(raw),substitutions,results};
}
const fixtureBindings=['tools/msi_native_hid_authority_selftest.mjs','tools/msi_native_hid_authority_selftest.cs','tools/msi_native_hid_authority_boundaries.cs','tools/fixtures/msi_game_authority_diagnostics_r114/Boundaries.cs'].map(file=>({file,sha256:sha(fs.readFileSync(path.join(repo,file)))}));
const bindings=[];
for(const variant of variants){
 const raw=fs.readFileSync(sourceFiles[variant]);
 if(variant==='legacy'){assert.equal(sha(raw),legacySha);write(path.join(output,'MsiNativePortCycle.legacy.cs'),raw);}
 if(variant==='preexisting'){assert.equal(sha(raw),preexistingSha);write(path.join(output,'MsiNativePortCycle.pre-existing-candidate.cs'),raw);}
 bindings.push({...compileAndRun(variant,raw,variant==='legacy'?'LEGACY':variant==='preexisting'?'PREEXISTING':''),source:sourceFiles[variant]});
}
const now=bindings.find(b=>b.variant==='current'),old=bindings.find(b=>b.variant==='legacy');
if(old&&now){
 const unchanged=rows=>rows.filter(row=>row.label.includes('/xusb/')||row.label.startsWith('msi/root-only/'));
 assert.deepEqual(unchanged(now.results.fullRunDifferential),unchanged(old.results.fullRunDifferential),'XUSB and root-only retain exact deployed traces and diagnostic bytes');
 const changedHidCases=now.results.fullRunDifferential.filter(row=>!row.label.includes('/xusb/')&&!row.label.startsWith('msi/root-only/'));
 write(path.join(output,'other-families-differential.json'),JSON.stringify({scope:'current HID paths use fresh capability authority; XUSB/root-only retain deployed behavior',unchangedExact:true,unchangedExactCases:unchanged(now.results.fullRunDifferential).length,currentHidCases:changedHidCases.length,directObservations:now.results.otherFamilies.length,fullRunResults:now.results.fullRunDifferential},null,2));
}
const negativeControls=[];
if(now&&!args.includes('--no-negative-controls')){
 const raw=fs.readFileSync(current),text=raw.toString('utf8');
 for(const [name,needle,replacement,expected] of [
  ['remove-post-open-rebinding',' || !HidInterfaceMatches(path,instance)','','mapping-changed authority'],
  ['allow-non-game-usage','caps.Usage is not (4 or 5)','false','keyboard authority']]){
  assert.equal(text.split(needle).length-1,1,'precise mutant '+name);
  const control=compileAndRun('negative-'+name,Buffer.from(text.replace(needle,replacement)),'',true);
  assert(control.results.failures.includes(expected),name+' must trip the intended assertion');negativeControls.push(control);
 }
}
write(path.join(output,'source-bindings.json'),JSON.stringify({completeSourceCompiled:true,realHardwareAccess:false,productEntryInvoked:false,fixtureBindings,bindings,negativeControls},null,2));
for(const {file,sha256} of fixtureBindings)assert.equal(sha(fs.readFileSync(path.join(repo,file))),sha256,'fixture stable: '+file);
if(now){
 assert.equal(sha(fs.readFileSync(current)),now.sha256,'source stable during validation');write(path.join(output,'MsiNativePortCycle.after.cs'),fs.readFileSync(current));
 for(const [name,file] of [['legacy',old?baseline:null],['preexisting',fs.existsSync(preexisting)?preexisting:null]])if(file){const diff=spawnSync('git',['diff','--no-index','--',file,current],{encoding:'utf8'});assert([0,1].includes(diff.status));write(path.join(output,`helper-vs-${name}.patch`),diff.stdout);}
}
if(!currentOnly){
 const evidence=path.resolve(repo,'../../../Build/Validation/G3E-VirtualToggle-20261018-220916/Extracted/native-lifecycle.log');
 if(fs.existsSync(evidence)){
  const rows=fs.readFileSync(evidence,'utf8').split(/\r?\n/).map((s,i)=>{try{return {line:i+1,record:JSON.parse(s)}}catch{return null}}).filter(Boolean);
  const identity=rows.find(({record:r})=>r.event==='physical-controller-identity'&&r.time==='2026-10-18T22:09:06.634');
  const denied=rows.find(({record:r})=>r.event==='input-host.native-helper-diagnostic'&&r.time==='2026-10-18T22:09:07.457');
  assert(identity&&denied);const a=identity.record,b=denied.record,f=b.helperDiagnostic.reportedFacts;
  assert.equal(a.deviceInstanceId,b.helperGameTarget);assert.equal(a.baseContainerDeviceInstanceId,b.sourceRoot);assert.equal(a.pid,b.pid);
  assert.equal(a.vendorId,0x0db0);assert.equal(a.productId,0x1901);assert.equal(a.usagePage,1);assert.equal(a.usage,5);
  assert.equal(b.helperExitCode,8);assert.equal(f.CompatibleCount,0);assert.equal(f.CompatibleData.Bytes,2);assert.equal(f.CompatibleData.Result,0);assert.equal(f.GameUsage,false);
  write(path.join(output,'现场契约复现.json'),JSON.stringify({source:evidence,sha256:sha(fs.readFileSync(evidence)),identity,denied,nonAtomicObservations:true,helperMustReadFreshCaps:true},null,2));
 }
}
