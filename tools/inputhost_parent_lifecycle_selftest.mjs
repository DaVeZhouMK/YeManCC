import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {extract} from './ymcc_sim/global/inputhost-protocol/extract-parser.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2); const option=(key,fallback)=>{const at=args.indexOf(key);return at<0?fallback:args[at+1];};
const out=path.resolve(option('--out','G:/YeManCC-Work/Build/Validation/VirtualController-Lifecycle-20261019/host/parent-current'));
const host=path.resolve(option('--source-root',path.join(repo,'InputHost')));
if(fs.existsSync(out))throw Error('Fresh output required: '+out);fs.mkdirSync(out,{recursive:true});
const inputs=[path.join(host,'Program.cs'),path.join(host,'ParentProcessLifetime.cs'),path.join(repo,'tools/inputhost_parent_lifecycle_selftest.cs'),fileURLToPath(import.meta.url),path.join(repo,'tools/ymcc_sim/global/inputhost-protocol/extract-parser.mjs'),path.join(host,'HostTerminalCleanup.cs')].map(file=>({file,bytes:fs.readFileSync(file)}));
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const text=inputs[0].bytes.toString('utf8');const fault=option('--fault','none');if(!['none','ignore-parent-cancellation'].includes(fault))throw Error('Unsupported fault');const pieces=['private static async Task<HostTerminalCleanup.LoopResult> RunPipeClientAsync(','private sealed class MessageCodec'].map(marker=>({...extract(text,marker),marker}));
fs.writeFileSync(path.join(out,'Extracted.cs'),'using System.Diagnostics; using System.IO.Pipes; using System.Text; using YeManInputHost.Diagnostics;\ninternal static partial class Program { private const int MaxMessageBytes=16384;\n'+pieces.map(p=>fault==='none'?p.text:p.text.replaceAll('parentLifetime.ExitToken','CancellationToken.None')).join('\n')+'\n}\n');
fs.writeFileSync(path.join(out,'HostTerminalCleanup.cs'),inputs.at(-1).bytes);fs.writeFileSync(path.join(out,'ParentProcessLifetime.cs'),inputs[1].bytes);fs.writeFileSync(path.join(out,'Test.cs'),inputs[2].bytes);
fs.writeFileSync(path.join(out,'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>');
fs.writeFileSync(path.join(out,'Fixture.csproj'),'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><UseAppHost>false</UseAppHost><NuGetAudit>false</NuGetAudit><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup></Project>');
const commands=[];let profiles=[];
for(const config of ['Release','Debug']){
 for(const [name,argv] of [['build',['build','Fixture.csproj','-c',config,'--nologo','-v:q']],['tests',[path.join(out,'bin',config,'net10.0/Fixture.dll')]]]){
 const r=spawnSync('dotnet',argv,{cwd:out,encoding:'utf8',timeout:60000,windowsHide:true});fs.writeFileSync(path.join(out,config+'-'+name+'.log'),(r.stdout??'')+(r.stderr??''));commands.push({name,config,argv,exit:r.status,error:r.error?.message??null});
 if(name==='build'&&r.status!==0)throw Error('Build failed: '+path.join(out,config+'-'+name+'.log'));
 if(name==='tests'){let result;try{result=JSON.parse(r.stdout.trim().split(/\r?\n/).at(-1));}catch{} profiles.push({config,exit:r.status,result});}
 }
}
const sourceStable=inputs.every(i=>sha(fs.readFileSync(i.file))===sha(i.bytes));
const result={fault,status:sourceStable&&profiles.every(p=>p.exit===0)?'PASS':'FAIL',sourceStable,profiles,commands,sources:inputs.map(i=>({path:i.file,sha256:sha(i.bytes)})),ranges:pieces.map(({text,...r})=>({...r,sha256:sha(text)})),productEntryExecuted:false,hardwareIo:false,sdkInputAssembliesLoaded:false,scope:'Exact production pipe loop and codec with actual ParentProcessLifetime; real BCL named pipes and owned helper child processes. Actual HostTerminalCleanup is invoked by the fixture continuation; HostSession, log and diagnostics remain explicit inert recorders. Product Main and real release resources are not executed.'};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({status:result.status,profiles:profiles.map(p=>({config:p.config,exit:p.exit,cases:p.result?.cases})),sourceStable}));if(result.status!=='PASS')process.exitCode=1;
