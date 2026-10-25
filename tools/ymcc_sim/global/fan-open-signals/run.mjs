import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';import {generate} from './extract.mjs';import {resolveBaseline,resolveCecil,parseArgs,BlockedInputError,baselineRelativeRoot,cecilToolProject} from './inputs.mjs';
export {getFanOpenAdapters} from './adapter.mjs';
const here=path.dirname(fileURLToPath(import.meta.url)),repo=path.resolve(here,'../../../..');
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
function files(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()&&!['bin','obj'].includes(e.name)?files(path.join(dir,e.name)):e.isFile()? [path.join(dir,e.name)]:[]);}
function run(exe,args,log,timeout=180000){const result=spawnSync(exe,args,{cwd:repo,windowsHide:true,encoding:'utf8',timeout,maxBuffer:4*1024*1024});fs.writeFileSync(log,(result.stdout??'')+'\n'+(result.stderr??'')+'\n'+(result.error?.message??''));if(result.status!==0)throw Error('command failed '+path.basename(log)+' exit='+result.status);return result;}
function artifactFiles(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()?artifactFiles(path.join(dir,e.name)):[path.join(dir,e.name)]);}
function find(root,name){const found=artifactFiles(root).filter(f=>path.basename(f)===name&&!f.includes(path.sep+'ref'+path.sep)&&!f.includes(path.sep+'refint'+path.sep)&&f.includes(path.sep+'bin'+path.sep));if(found.length!==1)throw Error('ambiguous '+name+': '+found.join(','));return found[0];}
export function execute(opts){const out=path.resolve(opts.evidenceDir);if(fs.existsSync(out))throw Error('Fresh evidence directory required');fs.mkdirSync(out,{recursive:true});
 const hc=path.resolve(opts.hcAssembly??path.join(repo,'PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1/HandheldCompanion.dll'));const payloadTemplateRoot=path.join(repo,'PowerControl/fan-host');
 const sourceFiles=[...files(path.join(repo,'FanLab/real-host')),...files(path.join(repo,'FanLab/fan-backend')),...files(here)].filter(f=>/\.(cs|csproj|mjs)$/.test(f)&&!f.endsWith('Fixture.template.cs'));
 sourceFiles.push(path.join(here,'Fixture.template.cs'),...['native/ProcessEfficiency.cs','FanLab/RuntimeDependencyContract.cs','FanLab/DeviceCoreSharedDependencies.cs'].map(f=>path.join(repo,f)));
 for(const relative of [cecilToolProject,...['YeManFanHost.dll','YeManFanBackend.dll','provenance.json'].map(name=>baselineRelativeRoot+'/'+name),...['install-fan-host-payload.ps1','run-emergency-fan-restore.ps1','YeManFanHost.authorization.md','YeManFanHost.payload.json'].map(name=>'PowerControl/fan-host/'+name)]){const file=path.join(repo,relative);if(fs.existsSync(file))sourceFiles.push(file);}
 const snapshot=()=>[...new Set(sourceFiles)].map(f=>({path:path.relative(repo,f).replaceAll('\\','/'),sha256:hash(f)}));const before=snapshot();let result;const inputEvidence={};
 try{
 const baseline=resolveBaseline({repoRoot:repo,legacyHostRoot:opts.legacyHostRoot});inputEvidence.baseline=baseline;const legacyRoot=baseline.root;
 const cecilEvidence=resolveCecil({repoRoot:repo,cecilPath:opts.cecilPath});inputEvidence.cecil=cecilEvidence;const cecil=cecilEvidence.path;
 if(!fs.existsSync(hc))throw new BlockedInputError('FAN_OPEN_HC_MISSING','HC reference assembly is missing; supply --hc-assembly',{path:hc});
 inputEvidence.hc={path:hc,sha256:hash(hc)};
 const generated=path.join(out,'generated');const derived=generate(repo,generated,path.join(here,'Fixture.template.cs'));
 const legacy=path.join(out,'legacy-host');fs.mkdirSync(legacy);for(const input of baseline.files){const copy=path.join(legacy,path.basename(input.path));fs.copyFileSync(input.path,copy);if(hash(copy)!==input.sha256)throw Error('Pre-fix baseline changed during copy: '+input.path);}
 const hostBuild=path.join(out,'host-build');run('dotnet',['build',path.join(repo,'FanLab/real-host/YeManFanHost.csproj'),'-c','Release','--artifacts-path',hostBuild,'--nologo','-v','quiet'],path.join(out,'host-build.log'));
 const built=find(hostBuild,'YeManFanHost.dll'),builtDir=path.dirname(built),payload=path.join(out,'host-payload');fs.cpSync(builtDir,payload,{recursive:true}); for(const name of ['install-fan-host-payload.ps1','run-emergency-fan-restore.ps1','YeManFanHost.authorization.md','YeManFanHost.payload.json']){const source=path.join(payloadTemplateRoot,name);if(fs.existsSync(source))fs.copyFileSync(source,path.join(payload,name));} const legacyManifest=path.join(payloadTemplateRoot,'YeManFanHost.payload.json'); if(fs.existsSync(legacyManifest)){const manifest=read(legacyManifest);manifest.files=manifest.files.filter(x=>fs.existsSync(path.join(payload,x.path))).map(x=>({...x,sha256:hash(path.join(payload,x.path))}));fs.writeFileSync(path.join(payload,'YeManFanHost.payload.json'),JSON.stringify(manifest,null,2));}

 const testBuild=path.join(out,'test-build');run('dotnet',['build',path.join(here,'Harness.csproj'),'-c','Release','--artifacts-path',testBuild,'-p:FixtureSource='+path.join(generated,'CompleteMsiFixture.g.cs'),'-p:CecilPath='+cecil,'--nologo','-v','quiet'],path.join(out,'test-build.log'));
 const harness=find(testBuild,'FanOpenSignals.dll');const test=spawnSync('dotnet',[harness,path.join(legacy,'YeManFanHost.dll'),path.join(payload,'YeManFanHost.dll'),hc,path.join(out,'behavior-result.json')],{cwd:out,windowsHide:true,encoding:'utf8',timeout:90000,maxBuffer:4*1024*1024});fs.writeFileSync(path.join(out,'behavior.log'),(test.stdout??'')+'\n'+(test.stderr??'')+'\n'+(test.error?.message??''));
 const report=fs.existsSync(path.join(out,'behavior-result.json'))?read(path.join(out,'behavior-result.json')):{};
 const sourceHashes=[...before,...Object.entries(derived).map(([path,sha256])=>({path,sha256}))];const changes=sourceHashes.filter(e=>hash(path.join(repo,e.path))!==e.sha256);
 const externalInputs=[inputEvidence.hc,...baseline.files,{path:cecil,sha256:cecilEvidence.sha256}];const externalInputChanges=externalInputs.filter(e=>!fs.existsSync(e.path)||hash(e.path)!==e.sha256);
 result={...report,schemaVersion:1,kind:'fan-open-signals',status:test.status===0&&report.status==='PASS'&&report.nativeAttempts===0&&!changes.length&&!externalInputChanges.length?'PASS':'FAIL',process:{exitCode:test.status,signal:test.signal,error:test.error?.message??null},sourceHashes,sourceChanges:changes,hostPayloadRoot:payload,hostPayloadFiles:files(payload).map(f=>({path:f,sha256:hash(f)})),inputEvidence,externalInputs,externalInputChanges,fixtureSourceManifest:path.join(generated,'fixture-source-manifest.json'),physicalHardwareValidated:false,fullProductAcceptance:false};
 }catch(error){result={schemaVersion:1,kind:'fan-open-signals',status:error instanceof BlockedInputError?'BLOCKED':'FAIL',code:error.code??null,error:error.stack,detail:error.detail??null,inputEvidence,sourceHashes:before,hardwareCalls:0,hostMainStarted:false,physicalHardwareValidated:false,fullProductAcceptance:false};}
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));return result;}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const opts = parseArgs(process.argv.slice(2));
    const result = execute(opts);
    console.log(JSON.stringify({ status: result.status, code: result.code, cases: result.cases?.length,
      hostPayloadRoot: result.hostPayloadRoot, error: result.error }));
    process.exitCode = result.status === 'PASS' ? 0 : result.status === 'BLOCKED' ? 3 : 2;
  } catch (error) {
    console.error(JSON.stringify({ status: 'FAIL', code: 'FAN_OPEN_ARGUMENT_ERROR', error: error.message }));
    process.exitCode = 2;
  }
}

