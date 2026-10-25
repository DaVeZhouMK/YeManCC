import{fileURLToPath}from'node:url';
import fs from 'node:fs';import path from 'node:path';import {spawnSync}from'node:child_process';import{createHash}from'node:crypto';
const opt={};for(let i=2;i<process.argv.length;i+=2){if(!process.argv[i+1])throw Error('argument value');opt[process.argv[i].slice(2)]=process.argv[i+1];}
const root=path.resolve(opt.repo??path.join(path.dirname(fileURLToPath(import.meta.url)),'..')),evidence=path.resolve(opt.out??'');if(!opt.out||fs.existsSync(evidence))throw Error('fresh --out required');fs.mkdirSync(evidence,{recursive:true});
const runtime=path.resolve(opt.runtime??root+'/deps/ymcc-shared-runtime'),dotnet=opt.dotnet??'dotnet';
const sha=b=>createHash('sha256').update(b).digest('hex'),esc=s=>s.replaceAll('&','&amp;').replaceAll('"','&quot;');
const faults=[
 ['wrong-address-space','native/BatteryHost/SecondaryDisplayController.cs',')(false);',')(true);'],
 ['rounded-instead-of-HC-floor','capabilities/HcSecondaryDisplayCapabilityCatalog.cs','* 255 / 100','* 255 + 50) / 100'],
 ['wrong-latch-register','capabilities/HcSecondaryDisplayCapabilityCatalog.cs','LatchRegister = 0x4F','LatchRegister = 0x4D'],
 ['skip-external-state-check','native/BatteryHost/SecondaryDisplayController.cs','BrightnessRegister)!=before)','BrightnessRegister)!=before&&session.Length==0)'],
 ['reuse-consumed-fence','native/BatteryHost/SecondaryDisplayController.cs','byte before=fencedRaw.Value;Invalidate();','byte before=fencedRaw.Value;revision++;'],
 ['skip-readback-match','native/BatteryHost/SecondaryDisplayController.cs','if(after!=target)','if(after!=target&&session.Length==0)'],
 ['eager-battery-domain','native/BatteryHost/HostDomainRouter.cs','Controller? battery;','Controller? battery=createBattery();'],
 ['skip-prebrightness-ready','native/BatteryHost/SecondaryDisplayController.cs','RequireReady();stage="brightness-write"','stage="brightness-write"'],
 ['admit-adjacent-keyboard-model','capabilities/HcSecondaryDisplayCapabilityCatalog.cs','string? model=board=="FLIP DS"?ModelId:','string? model=board=="FLIP KB"?ModelId:board=="FLIP DS"?ModelId:']
];
// The arithmetic fault must remain valid C# while changing the scaling formula only.
faults[1][2]='(byte)(Math.Clamp((int)brightness, 0, 100) * 255 / 100)';faults[1][3]='(byte)((Math.Clamp((int)brightness, 0, 100) * 255 + 50) / 100)';
const sourceNames=fs.readdirSync(root+'/native/BatteryHost').filter(x=>x.endsWith('.cs')).map(x=>'native/BatteryHost/'+x);
const projectText=fs.readFileSync(root+'/native/BatteryHost/YeManBatteryHost.csproj','utf8');for(const m of projectText.matchAll(/<Compile\s+Include="([^"]+)"/g))sourceNames.push(path.relative(root,path.resolve(root,'native/BatteryHost',m[1].replaceAll('\\','/'))).replaceAll('\\','/'));
sourceNames.push('tools/HcSecondaryDisplaySelfTest/Program.cs');const originals=new Map(sourceNames.map(n=>[n,fs.readFileSync(root+'/'+n)])),rows=[];
function run(exe,args,cwd){return spawnSync(exe,args,{cwd,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:8*1024*1024});}
const positives=[];
for(const mode of ['Release','Checked']){
 const result=run(dotnet,['run','--project',root+'/tools/HcSecondaryDisplaySelfTest/HcSecondaryDisplaySelfTest.csproj','-c',mode,'-p:YmccSharedRuntimeRoot='+runtime,...(mode==='Checked'?['-p:CheckForOverflowUnderflow=true']:[]),'--no-launch-profile'],root);
 fs.writeFileSync(evidence+'/'+mode+'.stdout.log',result.stdout??'');fs.writeFileSync(evidence+'/'+mode+'.stderr.log',result.stderr??'');
 const line=(result.stdout??'').trim().split('\n').findLast(x=>x.startsWith('{'));const report=line?JSON.parse(line):null;
 if(result.error||result.status!==0||report?.status!=='PASS')throw Error('positive consumer failed '+mode);positives.push({mode,exitCode:result.status,signal:result.signal,report});console.log(JSON.stringify({mode,...report}));
}
for(const [id,file,before,after]of faults){
 const dir=evidence+'/'+id;fs.mkdirSync(dir);const text=originals.get(file).toString('utf8');if(text.split(before).length!==2)throw Error('nonunique mutation anchor '+id);const mutated=text.replace(before,after),mutation=dir+'/'+path.basename(file);fs.writeFileSync(mutation,mutated);
 const sources=sourceNames.map(n=>'<Compile Include="'+esc(n===file?mutation:root+'/'+n)+'" />').join('\n');
 const project='<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><UseWindowsForms>true</UseWindowsForms><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems><StartupObject>Program</StartupObject><AssemblyName>BatteryHostTests</AssemblyName><UseSharedCompilation>false</UseSharedCompilation><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup><ItemGroup>'+sources+
 '<PackageReference Include="System.Management" Version="10.0.9" /><Reference Include="LibreHardwareMonitorLib"><HintPath>'+runtime+'/bin/LibreHardwareMonitorLib.dll</HintPath></Reference><Reference Include="RAMSPDToolkit-NDD"><HintPath>'+runtime+'/bin/RAMSPDToolkit-NDD.dll</HintPath></Reference>'+
 '<EmbeddedResource Include="'+root+'/native/BatteryHost/battery-routes.json" LogicalName="YeManBatteryHost.battery-routes.json" /><EmbeddedResource Include="'+root+'/native/BatteryHost/runtime-contract.json" LogicalName="YeManBatteryHost.runtime-contract.json" /></ItemGroup></Project>';
 fs.writeFileSync(dir+'/Fault.csproj',project);
 const build=run(dotnet,['build',dir+'/Fault.csproj','-c','Release','--nologo'],dir);fs.writeFileSync(dir+'/build.stdout.log',build.stdout??'');fs.writeFileSync(dir+'/build.stderr.log',build.stderr??'');
 if(build.error||build.status!==0)throw Error('fault did not compile '+id);
 const result=run(dotnet,[dir+'/bin/Release/net10.0-windows/BatteryHostTests.dll'],dir);fs.writeFileSync(dir+'/run.stdout.log',result.stdout??'');fs.writeFileSync(dir+'/run.stderr.log',result.stderr??'');
 const rejected=result.status!==0&&result.status!==null&&result.signal===null&&!result.error;
 const row={id,file,originalSha256:sha(originals.get(file)),faultSha256:sha(Buffer.from(mutated)),compiled:true,exitCode:result.status,signal:result.signal,rejected,firstFailure:(result.stderr??'').split('\n').find(x=>x.includes('Exception'))??null};rows.push(row);console.log(JSON.stringify(row));if(!rejected)throw Error('semantic source fault escaped '+id);
}
const drift=[...originals].filter(([name,b])=>sha(fs.readFileSync(root+'/'+name))!==sha(b)).map(([name])=>name);fs.writeFileSync(evidence+'/result.json',JSON.stringify({schemaVersion:1,status:rows.every(r=>r.rejected)&&!drift.length?'PASS':'FAIL',positives,faults:rows,sourceDrift:drift,allActualProductionSources:true,hardwareIo:false,productHostStarted:false,physicalAcceptance:false},null,2));if(drift.length)throw Error('source drift');