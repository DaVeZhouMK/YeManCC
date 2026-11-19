import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

// Explicit leaf of the existing NativeGameHidCaps original consumer.
// Actual Run15 executes with its existing inert OS/device ends, never an SDK.
export function runGpdColdLifetime({repo,values,runnerPath}){
 assert(values.has('--output'),'Explicit fresh output required');
 const out=path.resolve(values.get('--output'));assert(!fs.existsSync(out),'Fresh output required');
 const dotnet=process.env.YMCC_COLD_DOTNET;assert(dotnet&&path.isAbsolute(dotnet)&&fs.existsSync(dotnet),'Explicit absolute dotnet required');
 const deadline=Date.now()+180000;
 const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
 const pin=file=>{const b=fs.readFileSync(file);return {file,bytes:b.length,sha256:sha(b)};};
 const fixtures=path.join(repo,'tools/fixtures/native_game_hid_caps/Fixture.cs');
 const source=path.resolve(values.get('--source')||path.join(repo,'InputHost/MsiNativePortCycle.cs'));
 const names=['candidate',...(values.has('--baseline')?['baseline']:[])];
 const paths={candidate:source,...(values.has('--baseline')?{baseline:path.resolve(values.get('--baseline'))}:{})};
 const snapshots=new Map([fixtures,runnerPath,fileURLToPath(import.meta.url),dotnet,...Object.values(paths)].map(p=>[p,fs.readFileSync(p)]));
 const inputs=[...snapshots].map(([file,b])=>({file,bytes:b.length,sha256:sha(b)}));
 fs.mkdirSync(out,{recursive:true});
 const save=(file,obj)=>fs.writeFileSync(file,typeof obj==='string'?obj:JSON.stringify(obj,null,2)+'\n',{flag:'wx'});
 const report={status:'PREPARING',selection:'gpd-cold-lifetime',inputs,variants:[],realHardwareAccess:false,realSdkLoaded:false,actualRun15:true};
 save(path.join(out,'INPUT.json'),report);
 const run=(exe,args,cwd,name)=>{
  const timeout=Math.min(90000,deadline-Date.now());assert(timeout>0);
  const began=Date.now(),r=spawnSync(exe,args,{cwd,encoding:'utf8',windowsHide:true,timeout,maxBuffer:16*1024*1024,env:{...process.env,YMCC_COLD_FIXTURE_PARENT_PID:String(process.pid)}});
  save(path.join(cwd,name+'.stdout.log'),r.stdout||'');save(path.join(cwd,name+'.stderr.log'),r.stderr||'');
  const receipt={exe,args,exitCode:r.status,signal:r.signal,error:r.error?String(r.error):null,elapsedMs:Date.now()-began};save(path.join(cwd,name+'.PROCESS.json'),receipt);return receipt;
 };
 try{
  for(const name of names){
   const dir=path.join(out,name);fs.mkdirSync(dir);
   const raw=snapshots.get(paths[name]).toString('utf8');
   let actual=raw.replace('internal static class MsiNativePortCycle','internal static partial class MsiNativePortCycle').replace('using Microsoft.Win32;','using Registry = InertRegistry.Registry;');
   const imports=[...actual.matchAll(/^    \[DllImport[^\r\n]+$/gm)].map(r=>r[0]);assert.equal(imports.length,16);
   for(const declaration of imports){assert.equal(actual.split(declaration).length,2);actual=actual.replace(declaration,'');}
   assert(!actual.includes('DllImport'));
   save(path.join(dir,'Actual.cs'),actual);save(path.join(dir,'Fixture.cs'),snapshots.get(fixtures).toString('utf8'));
   save(path.join(dir,'Fixture.csproj'),'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors><NuGetAudit>false</NuGetAudit><UseSharedCompilation>false</UseSharedCompilation></PropertyGroup></Project>');
   const build=run(dotnet,['build',path.join(dir,'Fixture.csproj'),'--configuration','Release','--nologo','--verbosity','diagnostic','-bl:'+path.join(dir,'build.binlog')],dir,'build');assert.equal(build.exitCode,0,'Managed compile failure');
   const dll=path.join(dir,'bin/Release/net10.0/Fixture.dll');const terminal=path.join(dir,'RESULT.json');
   const execution=run(dotnet,[dll,terminal,'--case','gpd-cold-lifetime'],dir,'run');
   const result=JSON.parse(fs.readFileSync(terminal,'utf8'));assert.equal(result.selection,'gpd-cold-lifetime');assert.equal(result.cases,14);assert.equal(result.assertions,56);
   assert.equal(new Set(result.results.map(r=>r.fault)).size,14);
   if(name==='baseline'){
    assert.equal(execution.exitCode,1);assert(!result.passed);assert(result.failures.length>0);
    assert(result.failures.every(f=>f.includes('typed result=3 expected=13')),'Only actual prior early-lifetime result is the required counterexample');
   }else{assert.equal(execution.exitCode,0);assert(result.passed);assert.deepEqual(result.failures,[]);}
   report.variants.push({name,build,execution,result:pin(terminal),binary:pin(dll),rawResult:result,status:name==='baseline'?'EXPECTED_SEMANTIC_FAIL':'PASS'});
  }
  report.status='SCOPED_COMPLETE';
 }catch(error){report.status='FAIL';report.error=error.stack;throw error;}
 finally{
  report.closing=inputs.map(p=>({...p,currentSha256:pin(p.file).sha256}));report.drift=report.closing.filter(p=>p.sha256!==p.currentSha256);
  if(report.drift.length)report.status='INPUT_DRIFT';save(path.join(out,'result.json'),report);
 }
 assert.deepEqual(report.drift,[]);
 console.log(JSON.stringify({status:report.status,variants:report.variants.map(v=>({name:v.name,status:v.status,cases:v.rawResult.cases,assertions:v.rawResult.assertions,failures:v.rawResult.failures}))}));
}
