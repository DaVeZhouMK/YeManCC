// R114: compile the complete actual helper against inert CM/SDK/lifetime/BIOS
// boundaries, then compare baseline and candidate device-call traces exactly.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2);
const arg=(name,fallback)=>{const i=args.indexOf(name);return i<0?fallback:args[i+1];};
const output=path.resolve(arg('--output','G:/YeManCC-Work/Build/Validation/VirtualInput-OwnedExtensions-R114-20261016/helper-diagnostics'));
const baseline=path.resolve(arg('--baseline',path.join(output,'before/InputHost/MsiNativePortCycle.cs')));
const current=path.join(repo,'InputHost/MsiNativePortCycle.cs');
const fixtureDir=path.join(repo,'tools/fixtures/msi_game_authority_diagnostics_r114');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const write=(p,s)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,s);};
const sourceBindings=[];
function offlineSource(source,variant){
  const substitutions=[];
  function replace(from,to,label){assert.equal(source.split(from).length-1,1,label);source=source.replace(from,to);substitutions.push({label,from,to});}
  replace('using Microsoft.Win32;','using Registry = R114Offline.Registry;','inert BIOS registry boundary');
  const declarations=[...source.matchAll(/^    \[DllImport[^\n]+\] private static extern uint (\w+)\(([^\n]*)\);\r?$/gm)];
  assert.equal(declarations.length,8,'all and only CM extern declarations');
  const calls={Locate:'ref node,instance,flags',Property:'node,key.Format,key.Id,out type,buffer,ref bytes,flags',Parent:'out parent,node,flags',DeviceId:'node,identifier,length,flags',Disable:'node,flags',Enable:'node,flags',InterfaceSize:'out characters,ref type,instance,flags',Interfaces:'ref type,instance,buffer,characters,flags'};
  for(const m of declarations)replace(m[0],`    private static uint ${m[1]}(${m[2]}) => R114Offline.Boundary.${m[1]}(${calls[m[1]]});`,'inert CM '+m[1]);
  if(variant==='candidate')replace('System.Text.Json.JsonSerializer.Serialize(this)','R114Offline.Boundary.SerializeDiagnostic(this)','serializer fault injection; forwards real System.Text.Json by default');
  assert(!source.includes('DllImport'),'no real native entry point in offline assembly');
  return {source,substitutions};
}
// Mask literals/comments for exact method-span bindings; retain byte positions.
function mask(code){
  return code.replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*|@"(?:""|[^"])*"|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,m=>m.replace(/[^\r\n]/g,' '));
}
function span(source,name){
  const masked=mask(source),rx=new RegExp(`^    (?:internal|private) static [^\\n]*?\\b${name}\\(`,'m'),m=rx.exec(masked);
  assert(m,'actual method '+name);const start=m.index;let p=masked.indexOf('(',start),round=0;
  do{const c=masked[p++];if(c==='(')round++;if(c===')')round--;}while(round>0);
  while(/\s/.test(masked[p]))p++;
  let end;
  if(masked.slice(p,p+2)==='=>')end=masked.indexOf(';',p)+1;
  else {assert.equal(masked[p],'{',name);let depth=0;do{const c=masked[p++];if(c==='{')depth++;if(c==='}')depth--;}while(depth>0);end=p;}
  assert(end>start);const text=source.slice(start,end);
  return {name,line:source.slice(0,start).split('\n').length,bytes:Buffer.byteLength(text),sha256:hash(text),text};
}
const originals={baseline:fs.readFileSync(baseline),candidate:fs.readFileSync(current)};
assert.equal(hash(originals.baseline),'fc5b4dc9c80defb80a9772b9426d0d64588b9cd157d32fe3606aaef393969a05','frozen actual source binding');
const unchanged=['IsKnownMsi','ValidRoot','TryArguments','HasModel','KnownMachineRoot','CurrentBoardAllowed','PrepareDeviceCycle','ValidGameInstance','ValidXusbInstance','PrepareGameRefresh','RefreshGameNode','CurrentLocations','SelfTest'];
const methods=['Run','ExecuteVerified','GameHidAdmitted','GameXusbAdmitted','ActiveXusbInterface','CurrentGameHid',...unchanged];
const boundMethods={};
for(const [variant,bytes] of Object.entries(originals)){
  const source=bytes.toString('utf8');boundMethods[variant]=Object.fromEntries(methods.map(n=>[n,span(source,n)]));
  const transformed=offlineSource(source,variant);const dir=path.join(output,'offline',variant);
  write(path.join(dir,'MsiNativePortCycle.actual-offline.cs'),transformed.source);
  for(const file of ['Fixture.cs','Boundaries.cs'])write(path.join(dir,file),fs.readFileSync(path.join(fixtureDir,file)));
  write(path.join(dir,'Offline.csproj'),`<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems><NuGetAudit>false</NuGetAudit><TreatWarningsAsErrors>true</TreatWarningsAsErrors><AssemblyName>R114Offline</AssemblyName></PropertyGroup><ItemGroup><Compile Include="MsiNativePortCycle.actual-offline.cs"/><Compile Include="Fixture.cs"/><Compile Include="Boundaries.cs"/></ItemGroup></Project>\n`);
  sourceBindings.push({variant,sourcePath:variant==='baseline'?baseline:current,sourceSha256:hash(bytes),sourceBytes:bytes.length,offlineSourcePath:path.join(dir,'MsiNativePortCycle.actual-offline.cs'),offlineSourceSha256:hash(transformed.source),substitutions:transformed.substitutions,methods:Object.values(boundMethods[variant]).map(({text,...receipt})=>receipt)});
}
for(const name of unchanged)assert.equal(boundMethods.baseline[name].text,boundMethods.candidate[name].text,'unchanged actual method '+name);
write(path.join(output,'source-bindings.json'),JSON.stringify({schema:1,completeSourceCompiled:true,unchangedMethods:unchanged,bindings:sourceBindings},null,2)+'\n');
const summaries={};
for(const variant of ['baseline','candidate']){
  const dir=path.join(output,'offline',variant),project=path.join(dir,'Offline.csproj');
  const built=spawnSync('dotnet',['build',project,'--configuration','Release','--nologo','--verbosity','minimal'],{cwd:dir,encoding:'utf8',timeout:120000});
  write(path.join(dir,'compile.log'),(built.stdout??'')+(built.stderr??''));
  assert.equal(built.status,0,`${variant} offline fixture compile: ${built.stdout}\n${built.stderr}`);
  const receipt=path.join(dir,'results.json');
  const ran=spawnSync('dotnet',[path.join(dir,'bin/Release/net10.0/R114Offline.dll'),receipt],{cwd:dir,encoding:'utf8',timeout:60000});
  write(path.join(dir,'run.log'),(ran.stdout??'')+(ran.stderr??''));
  console.log(variant+': '+(ran.stdout??'').trim());
  assert.equal(ran.status,0,`${variant} offline cases: ${ran.stdout}\n${ran.stderr}`);
  summaries[variant]=JSON.parse(fs.readFileSync(receipt,'utf8'));
}
assert.equal(summaries.baseline.results.length,summaries.candidate.results.length);
let differential=0,totalCalls=0;
for(let i=0;i<summaries.baseline.results.length;i++){
  const a=summaries.baseline.results[i],b=summaries.candidate.results[i];
  assert.equal(a.Id,b.Id);assert.equal(a.Result,b.Result,a.Id+' result');
  assert.deepEqual(a.Calls,b.Calls,a.Id+' exact CM/SDK/lifetime/BIOS call order, count, args, returned values');
  differential++;totalCalls+=b.Calls.length;
}
assert.deepEqual(summaries.baseline.policyScans,summaries.candidate.policyScans,'exact actual predicate enumeration counts, values and short-circuit order');
const policyScanComparisons=summaries.candidate.policyScans.length;
assert.equal(summaries.candidate.maxParentSteps,16);
assert.equal(summaries.candidate.maxInterfaceAttempts,2);
assert(summaries.candidate.maxDiagnosticUtf8Bytes<=2048);
assert.equal(hash(fs.readFileSync(current)),hash(originals.candidate),'source remains bound to tested bytes');
write(path.join(output,'after/InputHost/MsiNativePortCycle.cs'),originals.candidate);
const diff=spawnSync('git',['diff','--no-index','--',baseline,current],{cwd:repo,encoding:'utf8'});
assert([0,1].includes(diff.status),'source diff');write(path.join(output,'helper-source.patch'),diff.stdout);
const receipt={schema:1,success:true,baselineSha256:hash(originals.baseline),candidateSha256:hash(originals.candidate),scenarios:differential,exactTraceComparisons:differential,policyScanComparisons,comparedBoundaryCalls:totalCalls,baselineAssertions:summaries.baseline.assertions,candidateAssertions:summaries.candidate.assertions,existingSelfTest:summaries.candidate.existingSelfTest,maxDiagnosticUtf8Bytes:summaries.candidate.maxDiagnosticUtf8Bytes,maxParentSteps:summaries.candidate.maxParentSteps,maxInterfaceAttempts:summaries.candidate.maxInterfaceAttempts,maxGuardUtf8Bytes:summaries.candidate.maxGuardUtf8Bytes,failures:0,productBuild:false,productHostOrSdkRun:false,realHardwareAccess:false,packagesOrDeployment:false,stdoutPrefix:'YMCC_MSI_GAME_AUTHORITY_DIAGNOSTIC ',recordByteCapIncludingPrefixAndNewline:2048};
write(path.join(output,'validation.json'),JSON.stringify(receipt,null,2)+'\n');
console.log(JSON.stringify(receipt));
