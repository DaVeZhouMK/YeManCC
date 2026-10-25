import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const runner=fileURLToPath(import.meta.url), repo=path.resolve(path.dirname(runner),'..');
const workspace=path.resolve(repo,'../../..');
const recovery=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/ProtocolRecovery');
const root=path.join(recovery,'TransportContract');
assert(process.argv.length===2,'No arguments; each run preserves a new evidence directory');
fs.mkdirSync(root,{recursive:true});
assert.equal(fs.realpathSync(root).toLowerCase(),path.resolve(root).toLowerCase(),'redirected evidence root');
const out=path.join(root,new Date().toISOString().replace(/[:.]/g,'-'));
fs.mkdirSync(out);
const load=f=>JSON.parse(fs.readFileSync(f,'utf8').replace(/^\uFEFF/,''));
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const blob=b=>crypto.createHash('sha1').update(Buffer.from(`blob ${b.length}\0`)).update(b).digest('hex');
const manifest=load(path.join(recovery,'acquisition-historical.json'));
const binaries=load(path.join(recovery,'shipped-binaries.json'));
const fixturePath=path.join(repo,'tools/fixtures/controller_hid_transport_contract/harness.cs');
const fixture=fs.readFileSync(fixturePath,'utf8');
const receipt={schema:1,status:'running',startedAt:new Date().toISOString(),
  scope:'static reference source methods executed only with an in-memory native-read substitute',
  productionChanged:false,shippedAssembliesExecuted:false,hidDeviceAccess:false,threadCreated:false,
  ymccRuntimeBugProven:false,sources:[],configurations:[],runnerSha256:sha(fs.readFileSync(runner)),fixtureSha256:sha(fixture)};
const save=()=>fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(receipt,null,2)+'\n');
function method(source,signature){
  const start=source.indexOf(signature); assert(start>=0,`missing ${signature}`);
  assert.equal(source.indexOf(signature,start+1),-1,`ambiguous ${signature}`);
  const open=source.indexOf('{',start); let depth=0;
  for(let i=open;i<source.length;++i){if(source[i]==='{')++depth;else if(source[i]==='}'&&--depth===0)return source.slice(start,i+1);}
  throw Error(`unterminated ${signature}`);
}
function run(args,cwd,log){
  const result=spawnSync('dotnet',args,{cwd,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:2*1024*1024,
    env:{...process.env,DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_NOLOGO:'1',DOTNET_CLI_HOME:path.join(out,'dotnet-home')}});
  fs.writeFileSync(path.join(cwd,log),(result.stdout||'')+(result.stderr||''));
  if(result.error)throw result.error;assert.equal(result.status,0,`${log} exited ${result.status}`);return result.stdout;
}
try {
  for(const variant of ['historical','shipped-decompiled']){
    const base=variant==='historical'?path.join(recovery,'historical-source/hidapi.net'):path.join(recovery,'decompiled/hidapi.net/hidapi');
    const sourcePath=path.join(base,'HidDevice.cs'), eventPath=path.join(base,'HidDeviceInputReceivedEventArgs.cs');
    const source=fs.readFileSync(sourcePath,'utf8'), event=fs.readFileSync(eventPath,'utf8');
    if(variant==='historical')for(const file of [sourcePath,eventPath]){
      const rel='hidapi.net/'+path.basename(file), expected=manifest.files.find(row=>row.path===rel), bytes=fs.readFileSync(file);
      assert(expected?.status==='verified',`unverified fixed source ${rel}`);assert.equal(sha(bytes),expected.sha256);assert.equal(blob(bytes),expected.gitBlob);
    }
    else {const bin=binaries.find(row=>row.name==='hidapi.net.dll');assert.equal(sha(fs.readFileSync(bin.copied)),bin.sha256);}
    const signatures=['public byte[] Read(int timeout = 100)','public int Read(byte[] buffer, int timeout = 100)','private void ReadLoop()'];
    const methods=signatures.map(signature=>({signature,code:method(source,signature)}));
    receipt.sources.push({variant,path:sourcePath,sha256:sha(source),eventPath,eventSha256:sha(event),
      methods:methods.map(m=>({signature:m.signature,sha256:sha(m.code)}))});save();
    const dir=path.join(out,variant);fs.mkdirSync(dir);
    fs.writeFileSync(path.join(dir,'Program.cs'),fixture.replace('// REFERENCE_METHODS',methods.map(m=>m.code).join('\n')));
    fs.writeFileSync(path.join(dir,'HidDeviceInputReceivedEventArgs.cs'),event);
    fs.writeFileSync(path.join(dir,'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>\n');
    fs.writeFileSync(path.join(dir,'TransportContract.csproj'),'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>disable</ImplicitUsings><Nullable>disable</Nullable><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup></Project>\n');
    for(const configuration of ['Release','Debug']){
      run(['build','TransportContract.csproj','--configuration',configuration,'--nologo','--verbosity','quiet'],dir,`${configuration}-build.log`);
      const text=run([path.join(dir,'bin',configuration,'net10.0/TransportContract.dll')],dir,`${configuration}-run.log`);
      const result=JSON.parse(text.trim());assert.equal(result.status,'pass');assert.equal(result.checks,601);assert.equal(result.shortReports,118);
      receipt.configurations.push({variant,configuration,...result});save();
    }
  }
  receipt.status='pass';receipt.finishedAt=new Date().toISOString();save();
  console.log(JSON.stringify({status:receipt.status,configurations:receipt.configurations.map(x=>({variant:x.variant,configuration:x.configuration,checks:x.checks})),out}));
} catch(error){receipt.status='failed';receipt.error=error.stack;save();throw error;}
