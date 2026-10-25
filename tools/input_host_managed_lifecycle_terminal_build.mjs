// Compile frozen production source without running Host or loading its SDK.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawnSync}from'node:child_process';import{fileURLToPath}from'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const root=path.resolve(process.argv[2]??'G:/YeManCC-Work/Testing/YMCC/Evidence/VC-HostManaged23/terminal-patch/production-build-001');
if(fs.existsSync(root))throw Error('Fresh build evidence required');fs.mkdirSync(root,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),rows=[];
function snapshot(source,destination){const bytes=fs.readFileSync(source);fs.mkdirSync(path.dirname(destination),{recursive:true});fs.writeFileSync(destination,bytes);rows.push({path:source,snapshot:destination,sha256:sha(bytes)});return bytes;}
const project=path.join(root,'source');fs.mkdirSync(project);
function walk(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?['obj','bin'].includes(e.name)?[]:walk(path.join(dir,e.name)):[path.join(dir,e.name)]);}
const host=path.join(repo,'InputHost');for(const file of walk(host).filter(f=>f.endsWith('.cs')))snapshot(file,path.join(project,path.relative(host,file)));
let csproj=snapshot(path.join(host,'YeManInputHost.csproj'),path.join(root,'YeManInputHost.original.csproj')).toString('utf8');
csproj=csproj.replaceAll('$(MSBuildThisFileDirectory)',host.replaceAll('\\','/')+'/');
const linked=[];csproj=csproj.replace(/<Compile Include="([^"]+)"([^>]*)\/>/g,(_,relative,attributes)=>{const source=path.resolve(host,relative.replaceAll('\\','/'));const dest=path.join(root,'linked',path.basename(source));snapshot(source,dest);linked.push({relative,source,destination:dest});return '<Compile Include="'+dest.replaceAll('\\','/')+'"'+attributes+'/>';});
fs.writeFileSync(path.join(project,'YeManInputHost.csproj'),csproj);fs.writeFileSync(path.join(project,'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>');
fs.writeFileSync(path.join(project,'global.json'),JSON.stringify({sdk:{version:'10.0.400',rollForward:'disable'}}));
const commands=[];for(const config of ['Release','Debug']){const argv=['build','YeManInputHost.csproj','-c',config,'--nologo','-v:q','-p:NuGetAudit=false'];const r=spawnSync('dotnet',argv,{cwd:project,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:16*1024*1024});fs.writeFileSync(path.join(root,config+'-build.log'),(r.stdout??'')+(r.stderr??''));commands.push({config,argv,exit:r.status,error:r.error?.message??null});}
const stable=rows.every(r=>sha(fs.readFileSync(r.path))===r.sha256);const result={status:stable&&commands.every(c=>c.exit===0)?'PASS':'FAIL',commands,sourceStable:stable,rows,linked,projectPathAdaptationOnly:true,productExecuted:false,realSdkExecuted:false};
fs.writeFileSync(path.join(root,'result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({status:result.status,commands,sourceStable:stable,result:path.join(root,'result.json')}));if(result.status!=='PASS')process.exitCode=1;
