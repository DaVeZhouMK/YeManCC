// Execution is explicit and split: verification is allowed independently;
// formal performance additionally needs a human-issued performance-window grant.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const workspace=path.resolve(repo,'../../..');
const root=path.join(workspace,'Build/Validation/VirtualInput-Cpu-Exhaustion-20261018/HostSony');
const args=process.argv.slice(2);if(args.length!==2||args[0]!=='--mode'||!['verify','formal'].includes(args[1]))throw Error('--mode verify|formal');
const mode=args[1],sha=x=>crypto.createHash('sha256').update(x).digest('hex');
const manifest=JSON.parse(fs.readFileSync(path.join(root,'PREPARED.json'),'utf8'));
const planFile=path.join(root,'acceptance-plan.json'),plan=JSON.parse(fs.readFileSync(planFile,'utf8'));
function bind(){for(const f of [...manifest.sourceFiles,...manifest.references,...manifest.tools])if(sha(fs.readFileSync(f.path))!==f.sha256)throw Error('Input drift, no implicit rebase: '+f.path);if(sha(fs.readFileSync(planFile))!==manifest.planSha256)throw Error('Frozen plan drift');}
bind();
const env={...process.env,DOTNET_CLI_HOME:path.join(root,'dotnet-home'),NUGET_PACKAGES:path.join(root,'nuget-packages'),DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_SKIP_FIRST_TIME_EXPERIENCE:'1',DOTNET_GENERATE_ASPNET_CERTIFICATE:'false',DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE:'true'};
const runDir=path.join(root,'Runs',mode+'-'+new Date().toISOString().replaceAll(/[:.]/g,'-'));fs.mkdirSync(runDir,{recursive:true});
function save(name,obj){fs.writeFileSync(path.join(runDir,name),JSON.stringify(obj,null,2)+'\n');}
function run(cmd,argv,log,timeout=180000,extraEnv={}){
 const result=spawnSync(cmd,argv,{cwd:path.join(root,'Projects'),encoding:'utf8',timeout,maxBuffer:16*1024*1024,env:{...env,...extraEnv}});
 fs.writeFileSync(path.join(runDir,log),(result.stdout??'')+(result.stderr??''));
 save(log+'.exit.json',{command:cmd,args:argv,exitCode:result.status,error:result.error?.message??null});
 if(result.status!==0||result.error)throw Error(log+' failed; preserved '+runDir);
}
try{
 if(mode==='verify'){
  run('dotnet',['restore','Runner/Runner.csproj','--configfile','NuGet.Config','-p:NuGetAudit=false'],'restore.log');
  const results=[];
  for(const configuration of plan.correctnessConfigurations){
   run('dotnet',['build','Runner/Runner.csproj','-c',configuration,'--no-restore','--nologo'],'build-'+configuration+'.log');
   const bin=path.join(root,'Projects/Runner/bin',configuration,'net10.0-windows/SonyReportBufferFixture.dll'),dest=path.join(runDir,configuration);
   run('dotnet',[bin,'verify',planFile,dest],'verify-'+configuration+'.log',240000);
   const data=JSON.parse(fs.readFileSync(path.join(dest,'result.json'),'utf8'));if(!data.pass)throw Error('Correctness failed');results.push({configuration,...data});
  }
  bind();const receipt={pass:true,mode,runDirectory:runDir,results,productionChanged:false,formalRuns:0,planSha256:manifest.planSha256,verifiedAt:new Date().toISOString()};save('result.json',receipt);fs.writeFileSync(path.join(root,'VERIFY-LATEST.json'),JSON.stringify(receipt,null,2)+'\n');console.log(JSON.stringify(receipt));
 }else{
  const grantFile=path.join(root,'PERFORMANCE-WINDOW-GRANT.json');if(!fs.existsSync(grantFile))throw Error('No explicit human-issued performance window grant. Formal execution is still withheld.');
  const grant=JSON.parse(fs.readFileSync(grantFile,'utf8'));if(grant.allowed!==true||grant.source!=='explicit-user-message'||grant.planSha256!==manifest.planSha256)throw Error('Invalid/outdated performance grant');
  const verification=JSON.parse(fs.readFileSync(path.join(root,'VERIFY-LATEST.json'),'utf8'));if(!verification.pass||verification.planSha256!==manifest.planSha256)throw Error('Current correctness receipt required');
  fs.writeFileSync(path.join(root,'FORMAL-CAMPAIGN-RESERVED.json'),JSON.stringify({reservedAt:new Date().toISOString(),runDirectory:runDir,planSha256:manifest.planSha256}),{flag:'wx'});
  const bin=path.join(root,'Projects/Runner/bin/Release/net10.0-windows/SonyReportBufferFixture.dll'),rows=[];
  for(const configuration of plan.configurations){const settings=configuration==='Release-no-tier'?{DOTNET_TieredCompilation:'0'}:{DOTNET_TieredCompilation:'1',DOTNET_TieredPGO:'1'};const dest=path.join(runDir,configuration);run('dotnet',[bin,'benchmark',planFile,dest],configuration+'.log',plan.formalProcessTimeoutSeconds*1000,settings);const data=JSON.parse(fs.readFileSync(path.join(dest,'result.json'),'utf8'));rows.push({configuration,...data});}
  bind();const receipt={pass:rows.every(r=>r.pass),performancePass:rows.every(r=>r.benchmark?.performancePass===true),runDirectory:runDir,rows,planSha256:manifest.planSha256,productionChanged:false};save('result.json',receipt);fs.writeFileSync(path.join(root,'FORMAL-LATEST.json'),JSON.stringify(receipt,null,2)+'\n');console.log(JSON.stringify({pass:receipt.pass,performancePass:receipt.performancePass,runDirectory:runDir}));
 }
}catch(error){save('failure.json',{mode,error:String(error),productionChanged:false,formalNotRetried:true});console.error(String(error));process.exitCode=1;}
