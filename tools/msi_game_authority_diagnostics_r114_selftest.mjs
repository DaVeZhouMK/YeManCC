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
const mode=arg('--mode','r114-frozen');
assert(['r114-frozen','current-status'].includes(mode),'explicit supported mode');
const currentMode=mode==='current-status';
assert(currentMode || !args.includes('--source'),'--source requires current-status mode');
const current=path.resolve(arg('--source',path.join(repo,'InputHost/MsiNativePortCycle.cs')));
const started=Date.now(),timings=[];
const fixtureDir=path.join(repo,'tools/fixtures/msi_game_authority_diagnostics_r114');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const write=(p,s)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,s);};
const sourceBindings=[];
function offlineSource(source,variant){
  const substitutions=[];
  function replace(from,to,label){assert.equal(source.split(from).length-1,1,label);source=source.replace(from,to);substitutions.push({label,from,to});}
  replace('using Microsoft.Win32;','using Registry = R114Offline.Registry;','inert BIOS registry boundary');
  if(currentMode){
    const declarations=[...source.matchAll(/^    \[DllImport[^\n]+\] (?:\[return:[^\n]+\] )?private static extern ([\w?]+) (\w+)\(([^\n]*)\);\r?$/gm)];
    assert.equal(declarations.length,variant==='baseline'?14:15,'all current OS declarations bound');
    const calls={Locate:'ref node,instance,flags',Property:'node,key.Format,key.Id,out type,buffer,ref bytes,flags',Parent:'out parent,node,flags',DeviceId:'node,identifier,length,flags',Disable:'node,flags',Enable:'node,flags',InterfaceSize:'out characters,ref type,instance,flags',Interfaces:'ref type,instance,buffer,characters,flags',DevNodeStatus:'out status,out problem,node,flags'};
    const unused=['InterfaceProperty','OpenHid','HidAttributesRead','HidPreparsedRead','HidCapabilitiesRead','HidPreparsedFree'];
    for(const m of declarations){
      assert(calls[m[2]] || unused.includes(m[2]),'known OS boundary '+m[2]);
      const body=calls[m[2]]?'R114Offline.Boundary.'+m[2]+'('+calls[m[2]]+')':'throw new InvalidOperationException("unreached inert HID boundary: '+m[2]+'")';
      replace(m[0],'    private static '+m[1]+' '+m[2]+'('+m[3]+') => '+body+';','inert current OS '+m[2]);
    }
    assert.equal(source.split('System.Text.Json.JsonSerializer.Serialize(this)').length-1,2,'both current diagnostic serializers');
    source=source.replaceAll('System.Text.Json.JsonSerializer.Serialize(this)','R114Offline.Boundary.SerializeDiagnostic(this)');
    substitutions.push({label:'both diagnostic serializers; forwards real System.Text.Json except explicit fault injection'});
    assert(!source.includes('DllImport'),'no unmanaged entry point in current offline assembly');
    return {source,substitutions};
  }
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
if(currentMode){
  for(const variant of ['baseline','candidate']){
    const expected=arg('--'+variant+'-sha256','');
    assert(/^[a-f0-9]{64}$/i.test(expected),'explicit '+variant+' SHA256 required');
    assert.equal(hash(originals[variant]),expected.toLowerCase(),variant+' explicit source SHA256 binding');
  }
}else assert.equal(hash(originals.baseline),'fc5b4dc9c80defb80a9772b9426d0d64588b9cd157d32fe3606aaef393969a05','frozen actual source binding');
const unchanged=['IsKnownMsi','ValidRoot','TryArguments','HasModel','KnownMachineRoot','CurrentBoardAllowed','PrepareDeviceCycle','ValidGameInstance','ValidXusbInstance','PrepareGameRefresh','RefreshGameNode','CurrentLocations','SelfTest'];
if(currentMode){
  unchanged.splice(unchanged.indexOf('PrepareGameRefresh'),1);
  unchanged.push('Run','ExecuteVerified','GameNodeRefreshMutationAllowed','CurrentGameHid','GameHidAdmitted','GameXusbAdmitted','ActiveXusbInterface');
  function permittedSource(source){
    const masked=mask(source),start=source.indexOf('    internal sealed class GameRefreshFacts');
    assert(start>=0);let p=masked.indexOf('{',start),depth=0;
    do{const c=masked[p++];if(c==='{')depth++;if(c==='}')depth--;}while(depth>0);
    return source.slice(0,start)+'<GameRefreshFacts>'+source.slice(p);
  }
  function outsideAllowed(source){
    source=source.replace(span(source,'PrepareGameRefresh').text,'<PrepareGameRefresh>');
    source=permittedSource(source);
    return source.replace(/^    \[DllImport\("cfgmgr32.dll",EntryPoint="CM_Get_DevNode_Status"\)\] private static extern uint DevNodeStatus\(out uint status,out uint problem,uint node,uint flags\);\r?\n/m,'');
  }
  assert.equal(outsideAllowed(originals.baseline.toString('utf8')),outsideAllowed(originals.candidate.toString('utf8')),'product source changes limited to authorized class/method/status declaration');
}
const methods=['Run','ExecuteVerified','GameHidAdmitted','GameXusbAdmitted','ActiveXusbInterface','CurrentGameHid',...unchanged];
const boundMethods={};
for(const [variant,bytes] of Object.entries(originals)){
  const source=bytes.toString('utf8');boundMethods[variant]=Object.fromEntries(methods.map(n=>[n,span(source,n)]));
  const transformed=offlineSource(source,variant);const dir=path.join(output,'offline',variant);
  write(path.join(dir,'MsiNativePortCycle.actual-offline.cs'),transformed.source);
  for(const file of ['Fixture.cs','Boundaries.cs'])write(path.join(dir,file),fs.readFileSync(path.join(fixtureDir,file)));
  write(path.join(dir,'Offline.csproj'),`<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems><NuGetAudit>false</NuGetAudit><TreatWarningsAsErrors>true</TreatWarningsAsErrors><AssemblyName>R114Offline</AssemblyName><DefineConstants>${currentMode?'CURRENT_STATUS':''}</DefineConstants></PropertyGroup><ItemGroup><Compile Include="MsiNativePortCycle.actual-offline.cs"/><Compile Include="Fixture.cs"/><Compile Include="Boundaries.cs"/></ItemGroup></Project>\n`);
  sourceBindings.push({variant,sourcePath:variant==='baseline'?baseline:current,sourceSha256:hash(bytes),sourceBytes:bytes.length,offlineSourcePath:path.join(dir,'MsiNativePortCycle.actual-offline.cs'),offlineSourceSha256:hash(transformed.source),substitutions:transformed.substitutions,methods:Object.values(boundMethods[variant]).map(({text,...receipt})=>receipt)});
}
const fixtureBindings=['tools/msi_game_authority_diagnostics_r114_selftest.mjs',...['Fixture.cs','Boundaries.cs','README.md','diagnostic.schema.json'].map(f=>'tools/fixtures/msi_game_authority_diagnostics_r114/'+f)].map(file=>({file,sha256:hash(fs.readFileSync(path.join(repo,file)))}));
for(const name of unchanged)assert.equal(boundMethods.baseline[name].text,boundMethods.candidate[name].text,'unchanged actual method '+name);
write(path.join(output,'source-bindings.json'),JSON.stringify({schema:1,completeSourceCompiled:true,unchangedMethods:unchanged,bindings:sourceBindings},null,2)+'\n');
const summaries={};
if(currentMode){
  const info=spawnSync('dotnet',['--info'],{encoding:'utf8',timeout:10000});
  write(path.join(output,'dotnet-info.txt'),(info.stdout??'')+(info.stderr??''));assert.equal(info.status,0);
}
for(const variant of ['baseline','candidate']){
  const dir=path.join(output,'offline',variant),project=path.join(dir,'Offline.csproj');
  const buildStart=Date.now();
  const built=spawnSync('dotnet',['build',project,'--configuration','Release','--nologo','--verbosity','minimal'],{cwd:dir,encoding:'utf8',timeout:120000});
  write(path.join(dir,'compile.log'),(built.stdout??'')+(built.stderr??''));
  assert.equal(built.status,0,`${variant} offline fixture compile: ${built.stdout}\n${built.stderr}`);
  timings.push({variant,phase:'build',milliseconds:Date.now()-buildStart});
  const runStart=Date.now();
  const receipt=path.join(dir,'results.json');
  const ran=spawnSync('dotnet',[path.join(dir,'bin/Release/net10.0/R114Offline.dll'),receipt],{cwd:dir,encoding:'utf8',timeout:60000});
  write(path.join(dir,'run.log'),(ran.stdout??'')+(ran.stderr??''));
  console.log(variant+': '+(ran.stdout??'').trim());
  assert.equal(ran.status,0,`${variant} offline cases: ${ran.stdout}\n${ran.stderr}`);
  timings.push({variant,phase:'execute',milliseconds:Date.now()-runStart});
  summaries[variant]=JSON.parse(fs.readFileSync(receipt,'utf8'));
}
assert.equal(summaries.baseline.results.length,summaries.candidate.results.length);
let differential=0,totalCalls=0;
for(let i=0;i<summaries.baseline.results.length;i++){
  const a=summaries.baseline.results[i],b=summaries.candidate.results[i];
  assert.equal(a.Id,b.Id);assert.equal(a.Result,b.Result,a.Id+' result');
  if(currentMode){
    const diagnostic=c=>c.startsWith('diagnostic-locate:') || c.startsWith('devnode-status:');
    const writes=c=>c.startsWith('disable:') || c.startsWith('enable:') || c==='sdk-cycle';
    assert.deepEqual(a.Calls.filter(writes),b.Calls.filter(writes),a.Id+' exact actual device writes');
    assert.deepEqual(a.Calls,b.Calls.filter(c=>!diagnostic(c)),a.Id+' exact pre-existing calls');
  }else assert.deepEqual(a.Calls,b.Calls,a.Id+' exact CM/SDK/lifetime/BIOS call order, count, args, returned values');
  differential++;totalCalls+=b.Calls.length;
}
assert.deepEqual(summaries.baseline.policyScans,summaries.candidate.policyScans,'exact actual predicate enumeration counts, values and short-circuit order');
const policyScanComparisons=summaries.candidate.policyScans.length;
if(!currentMode){
  assert.equal(summaries.candidate.maxParentSteps,16);
  assert.equal(summaries.candidate.maxInterfaceAttempts,2);
}else{
  assert.equal(summaries.candidate.maxGuardUtf8Bytes,2048);
  const writes=c=>c.startsWith('disable:') || c.startsWith('enable:') || c==='sdk-cycle';
  write(path.join(output,'device-write-traces.json'),JSON.stringify(summaries.candidate.results.map((b,i)=>({id:b.Id,result:b.Result,baseline:summaries.baseline.results[i].Calls.filter(writes),candidate:b.Calls.filter(writes),same:true,baselineCalls:summaries.baseline.results[i].Calls,candidateCalls:b.Calls})),null,2)+'\n');
}
assert(summaries.candidate.maxDiagnosticUtf8Bytes<=2048);
assert.equal(hash(fs.readFileSync(current)),hash(originals.candidate),'source remains bound to tested bytes');
write(path.join(output,'after/InputHost/MsiNativePortCycle.cs'),originals.candidate);
const diff=spawnSync('git',['diff','--no-index','--',baseline,current],{cwd:repo,encoding:'utf8'});
assert([0,1].includes(diff.status),'source diff');write(path.join(output,'helper-source.patch'),diff.stdout);
assert.equal(hash(fs.readFileSync(baseline)),hash(originals.baseline),'baseline remains bound');
for(const binding of fixtureBindings)assert.equal(hash(fs.readFileSync(path.join(repo,binding.file))),binding.sha256,'fixture remains bound: '+binding.file);
const receipt={schema:1,mode,success:true,executedThisRun:true,fixtureBindings,arguments:args,timings,totalMilliseconds:Date.now()-started,scope:currentMode?'whole-helper inert XUSB refresh, diagnostic boundaries, unchanged SelfTest and policy enumeration; HID OS paths not executed':'frozen R114 differential',baselineSha256:hash(originals.baseline),candidateSha256:hash(originals.candidate),scenarios:differential,exactTraceComparisons:currentMode?undefined:differential,exactDeviceWriteComparisons:currentMode?differential:undefined,exactExistingCallComparisons:currentMode?differential:undefined,policyScanComparisons,comparedBoundaryCalls:totalCalls,baselineAssertions:summaries.baseline.assertions,candidateAssertions:summaries.candidate.assertions,existingSelfTest:summaries.candidate.existingSelfTest,maxDiagnosticUtf8Bytes:summaries.candidate.maxDiagnosticUtf8Bytes,maxParentSteps:summaries.candidate.maxParentSteps,maxInterfaceAttempts:summaries.candidate.maxInterfaceAttempts,maxGuardUtf8Bytes:summaries.candidate.maxGuardUtf8Bytes,failures:0,productBuild:false,productHostOrSdkRun:false,realHardwareAccess:false,packagesOrDeployment:false,stdoutPrefix:'YMCC_MSI_GAME_AUTHORITY_DIAGNOSTIC ',recordByteCapIncludingPrefixAndNewline:2048};
write(path.join(output,'validation.json'),JSON.stringify(receipt,null,2)+'\n');
console.log(JSON.stringify(receipt));
