// Registered offline consumer. Current 2.0 and historical 1.12 executables use
// distinct method fixtures and contracts; neither loads the real SDK assembly.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {MODULE, REPO, INPUTS, artifacts} from './adapter.mjs';
const self=fileURLToPath(import.meta.url);
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const hash=file=>sha(fs.readFileSync(file));
const need=(ok,message)=>{if(!ok)throw Error(message);};
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const text=file=>fs.readFileSync(file,'utf8').replace(/^\uFEFF/,'').replace(/\r\n/g,'\n');
const inside=(root,value)=>{const r=path.relative(root,value);return r!==''&&r!=='..'&&!r.startsWith('..'+path.sep)&&!path.isAbsolute(r);};
function walk(root){return fs.readdirSync(root,{withFileTypes:true}).flatMap(e=>{need(!e.isSymbolicLink(),'Evidence link refused');const p=path.join(root,e.name);return e.isDirectory()?walk(p):[p];});}
export function parseArgs(argv){need(argv.length===2&&argv[0]==='--evidence-dir'&&path.isAbsolute(argv[1]),'Use --evidence-dir ABS_NEW_DIRECTORY');return path.resolve(argv[1]);}
function block(source,start){const mask=source.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"/g,s=>s.replace(/[^\r\n]/g,' '));let p=mask.indexOf('{',start),depth=0;need(p>=0,'Missing body');for(;p<mask.length;p++){if(mask[p]==='{')depth++;if(mask[p]==='}'&&--depth===0)return source.slice(start,p+1);}throw Error('Unclosed body');}
function member(source,name){const matches=[...source.matchAll(new RegExp('^[ \\t]*(?:private|public|internal) (?:static )?[^\\r\\n;{}=]*\\b'+name+'\\s*\\(','gm'))];need(matches.length===1,'Nonunique extraction '+name);return block(source,matches[0].index);}
function guard(source,anchor){const start=source.indexOf(anchor);need(start>=0,'Guard missing '+anchor);return block(source,start);}
export function runSuite(requestedRoot){
 const root=path.resolve(requestedRoot),allowed=path.resolve(REPO,'../../../Build');need(inside(allowed,root),'Evidence outside workspace Build');
 for(let a=root;;a=path.dirname(a)){if(fs.existsSync(a))need(!fs.lstatSync(a).isSymbolicLink(),'Evidence reparse point refused');if(path.dirname(a)===a)break;}
 need(!fs.existsSync(root),'Fresh evidence required');fs.mkdirSync(root,{recursive:true});
 const sources=INPUTS.map(relative=>{const file=path.join(REPO,relative),bytes=fs.readFileSync(file),snapshot=path.join(root,'sources',relative);fs.mkdirSync(path.dirname(snapshot),{recursive:true});fs.writeFileSync(snapshot,bytes,{flag:'wx'});return {path:file,relative,sha256:sha(bytes),snapshot};});
 const read=relative=>text(sources.find(r=>r.relative===relative).snapshot);
 const spec=JSON.parse(read('tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json')),dotnet=path.join(spec.dotnetRoot,'dotnet.exe');
 const toolchain=artifacts().map(file=>({path:file,sha256:hash(file)}));need(toolchain.length<=640&&toolchain.some(r=>r.path.toLowerCase()===dotnet.toLowerCase()),'Toolchain closure invalid');
 write(path.join(root,'inputs-before.json'),{sources,toolchain});
 const sdk=path.join(REPO,'deps/hidmaestro/HIDMaestro.Core.dll'),sdkSha=hash(sdk);need(sdkSha==='8fe7bdb21ec497591f527b7b87c4bc7b41b45c35a06cc211a7a0dce48838ed2c','Pinned 2.0 SDK mismatch');
 const oldDispose=sources.find(r=>r.relative==='tools/fixtures/hidmaestro/1.12.0/HMController.Dispose.cs');
 const before=sources.find(r=>r.relative===MODULE+'/fixtures/HostControllerOutputStop.before.cs');
 need(oldDispose.sha256==='aa8d4088147e628041396827e4dc8471442e7c4abd770d74c742156982efb671','Frozen 1.12 Dispose changed');
 need(before.sha256==='ef4e54570bba0a55b357114573668e6f2f9f526f701c169854342c558a06fcf3','Frozen before helper changed');
 const beforeText=read(MODULE+'/fixtures/HostControllerOutputStop.before.cs');
 need(beforeText.includes('new Version(1, 12, 0, 0)')&&beforeText.includes('5f1d5fa1-a18d-44e5-aba0-a8dfb3fabdf8'),'Legacy identity relabeled');
 const fixtureRoot='tools/fixtures/hidmaestro/2.0.0/',bindingRow=sources.find(r=>r.relative===fixtureRoot+'binary-binding.json');
 need(bindingRow.sha256==='0c39be0028f002f733ab5773f90f43f743cd817b94a787111dc86a1cd0e59f8d','Frozen 2.0 method binding changed');
 const binding=JSON.parse(read(fixtureRoot+'binary-binding.json'));
 need(binding.sdkSha256===sdkSha&&binding.mvid==='86aed9a3-c024-4596-b02c-650082ffb852'&&binding.assemblyVersion==='2.0.0.0','2.0 fixture identity mismatch');
 for(const [file,entry]of Object.entries(binding.members))need(sources.find(r=>r.relative===fixtureRoot+file)?.sha256===entry.sha256,'Exact 2.0 method changed '+file);
 const screen=read('InputHost/ScreenTouchpadReport.cs'),program=read('InputHost/Program.cs');
 const fields=screen.match(/^    private static readonly FieldInfo\? ScreenPadLastEncoded[^\r\n]+/gm);need(fields?.length===1,'ScreenPad last-report contract changed');
 const originalTakeover=member(screen,'TakeScreenPadReportOwnership');
 const dispatch='YeManInputHost.Diagnostics.HostControllerIdleCtsRelease.TryStopInputClock';need(originalTakeover.split(dispatch).length===2,'Raw clock call target changed');
 const takeover=originalTakeover.replace(dispatch,'BclClockAdmission.TryStopInputClock');
 const releaseGuard=guard(member(program,'ReleaseLocalLocked'),'if (_sdkDisposeFailure.Length != 0)');
 const prepare=member(program,'TryPrepare'),prepareGuards=guard(prepare,'if (_sdkDisposeFailure.Length != 0)')+'\n'+guard(prepare,'if (_idleCtsReleasePending || _idleCtsRelease is not null)');
 const replacements={
  '@@EXACT_CONTROLLER_STOP@@':Object.keys(binding.members).filter(n=>n.startsWith('HMController.')).map(n=>read(fixtureRoot+n)).join('\n'),
  '@@EXACT_CLOCK_STOP@@':['DeviceClock.Stop.cs','DeviceClock.Wake.cs','DeviceClock.Close.cs'].map(n=>read(fixtureRoot+n)).join('\n'),
  '@@IDLE_FIELDS@@':fields.join('\n'),'@@IDLE_TAKEOVER@@':takeover,'@@IDLE_RELEASE@@':member(screen,'StopSteamDeckIdleForRelease'),
  '@@ACTUAL_IDLE_WRAPPER@@':member(program,'TryStopSteamDeckIdleForRelease'),'@@ACTUAL_COMPLETE_RECEIPT@@':member(program,'TryCompleteIdleCtsRelease'),
  '@@ACTUAL_RETAIN_FAILURE@@':member(program,'RetainSdkDisposeFailure'),'@@RELEASE_FAILURE_GUARD@@':releaseGuard,'@@PREPARE_GUARDS@@':prepareGuards
 };
 let fixture=read(MODULE+'/BclStopFixture.cs.in');
 for(const [token,value]of Object.entries(replacements)){need(fixture.split(token).length===2,'Template drift '+token);fixture=fixture.replace(token,()=>value);}
 const legacy=read(MODULE+'/Legacy112StopFixture.cs.in').replace('@@LEGACY112_DISPOSE@@',()=>read(oldDispose.relative));
 need(!fixture.includes('@@')&&!legacy.includes('@@'),'Unresolved fixture token');
 write(path.join(root,'consumer-extractions.json'),{actualSourceSnapshots:true,currentGeneration:'2.0.0',legacyGeneration:'1.12.0',
  rawOwnershipAdmissionSubstitution:{from:dispatch,to:'BclClockAdmission.TryStopInputClock',reason:'fake target metadata uses actual helper IRuntime seam; helper sources and consumer branches unchanged'},
  snippets:Object.entries(replacements).map(([token,source])=>({token,sha256:sha(source)})),legacyDisposeSha256:oldDispose.sha256});
 const output=read('InputHost/HostControllerOutputStop.cs'),idle=read('InputHost/HostControllerIdleCtsRelease.cs');
 const steps=[],profiles=[];
 function command(name,args,cwd){const r=spawnSync(dotnet,args,{cwd,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:16*1024*1024,env:{...process.env,DOTNET_ROOT:spec.dotnetRoot,DOTNET_NOLOGO:'1',DOTNET_CLI_TELEMETRY_OPTOUT:'1'}});fs.writeFileSync(path.join(cwd,name+'.stdout.log'),r.stdout??'');fs.writeFileSync(path.join(cwd,name+'.stderr.log'),r.stderr??'');steps.push({name,executable:dotnet,args,cwd,pid:r.pid,exit:r.status,signal:r.signal??null,error:r.error?.message??null});need(!r.error&&!r.signal&&Number.isInteger(r.status),'Process did not exit naturally '+cwd);return r;}
 function buildAndRun(label,{outputSource=output,idleSource=idle,fixtureSource=fixture,config='Release',historical=false}={}){
  const dir=path.join(root,label);fs.mkdirSync(dir);fs.writeFileSync(path.join(dir,'Fixture.cs'),fixtureSource);fs.writeFileSync(path.join(dir,'HostControllerOutputStop.cs'),outputSource);
  if(!historical){fs.writeFileSync(path.join(dir,'HostControllerIdleCtsRelease.cs'),idleSource);fs.writeFileSync(path.join(dir,'PinnedSdkProof.cs'),read(MODULE+'/PinnedSdkProof.cs'));}
  fs.writeFileSync(path.join(dir,'NuGet.Config'),'<configuration><packageSources><clear/></packageSources></configuration>');
  fs.writeFileSync(path.join(dir,'Fixture.csproj'),'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>disable</ImplicitUsings><Nullable>enable</Nullable><UseAppHost>false</UseAppHost><NuGetAudit>false</NuGetAudit><EnableNETAnalyzers>false</EnableNETAnalyzers><TreatWarningsAsErrors>true</TreatWarningsAsErrors><UseSharedCompilation>false</UseSharedCompilation></PropertyGroup></Project>');
  const build=command('build',['build','Fixture.csproj','-c',config,'--nologo','-v:q'],dir);need(build.status===0,'BUILD_FAILED:'+dir);
  const run=command('tests',[path.join(dir,'bin',config,'net10.0/Fixture.dll'),...historical?[]:[sdk]],dir);let results;try{results=JSON.parse(run.stdout);}catch{}
  const expected=(fixtureSource.match(/\bTest\("/g)||[]).length;
  need(results?.cases?.length===expected&&results.cases.every(r=>typeof r.pass==='boolean'),'Incomplete assertion receipt '+dir);
  const failures=results.cases.filter(r=>!r.pass);need(results.failed===failures.length&&results.passed+failures.length===expected,'Inconsistent assertion counts');
  need(results.realSdkLoaded===false&&results.realSdkControllerConstructed===false&&results.deviceDriverOrRealMappingAccess===false&&results.exactSdkDisposeBody===true&&results.realBclThreads===true,'Fixture boundary mismatch');
  write(path.join(dir,'results.json'),results);const profile={label,config,generation:historical?'1.12.0-frozen':'2.0.0',exit:run.status,results,dir};profiles.push(profile);return profile;
 }
 try{
  for(const config of ['Release','Debug']){const p=buildAndRun('current-'+config,{config});need(p.exit===0&&p.results.failed===0,'CURRENT_CONSUMER_FAILED:'+p.dir);}
  for(const config of ['Release','Debug']){
   const p=buildAndRun('before112-'+config,{config,historical:true,outputSource:beforeText,fixtureSource:legacy});const failures=p.results.cases.filter(r=>!r.pass);
   need(p.exit===1&&failures.length===2&&failures.some(r=>r.error.includes('unstarted-zero-view must prove no writer'))&&failures.some(r=>r.error.includes('idle CTS handle must close after normal SDK Dispose')),'Historical 1.12 gaps not reproduced');
  }
  const mutations=[
   {label:'negative-output-closed-cancel',target:'outputSource',from:'if (!closed)',to:'if (true)',test:'closed output CTS still requires real reader termination'},
   {label:'negative-idle-timeout',target:'idleSource',from:'if (!Stopped(runtime, thread, unstarted)) return false;',to:'if (!Stopped(runtime, thread, unstarted) && false) return false;',test:'input clock timeout cannot become raw ownership success'},
   {label:'negative-early-receipt',target:'idleSource',from:'if (!_disposeReturned) return false;',to:'if (!_disposeReturned && _controller is null) return false;',test:'SDK Dispose throw after disposed flag never authorizes receipt'},
   {label:'negative-dispose-failure-guard',target:'fixtureSource',from:releaseGuard,to:releaseGuard.replace('if (_sdkDisposeFailure.Length != 0)','if (_sdkDisposeFailure.Length == int.MinValue)'),test:'SDK Dispose throw remains poisoned for retry and prepare'}
  ];
  for(const m of mutations){const original=m.target==='outputSource'?output:m.target==='idleSource'?idle:fixture;need(original.includes(m.from),'Fault anchor changed '+m.label);const p=buildAndRun(m.label,{[m.target]:original.replace(m.from,()=>m.to)});need(p.exit===1&&p.results.cases.some(r=>!r.pass&&r.name===m.test),'Fault not caught '+m.label);}
  need(sources.every(r=>hash(r.path)===r.sha256&&hash(r.snapshot)===r.sha256),'Sources changed during suite');need(toolchain.every(r=>hash(r.path)===r.sha256),'SDK/compiler/runtime changed during suite');
  const result={schemaVersion:2,id:'controller.host-sdk-stop',status:'PASS',passed:true,sourcesStable:true,sources,
   sdk:{path:sdk,sha256:sdkSha,assemblyVersion:'2.0.0.0',mvid:binding.mvid,loaded:false},
   before:{path:before.path,sha256:before.sha256,assemblyVersion:'1.12.0.0',mvid:'5f1d5fa1-a18d-44e5-aba0-a8dfb3fabdf8',disposeSha256:oldDispose.sha256,meaning:'Frozen 1.12 helper plus 1.12 Dispose, separate historical negative executable; never relabeled as 2.0.'},
   profiles:profiles.map(({results,...p})=>({...p,passed:results.passed,failed:results.failed,failedCases:results.cases.filter(r=>!r.pass),observations:results.observations})),steps,toolchain,
   sourceHashes:[...sources.map(r=>({path:r.relative,sha256:r.sha256})),...walk(root).map(file=>({path:file,sha256:hash(file)}))],
   actualBclWorkers:true,controllerAndContextAreFakes:true,productMainExecuted:false,realSdkLoaded:false,hardwareIo:false,
   boundary:{realBclWorkers:true,realSdkWorkers:false,sdkLoaded:false,productMainExecuted:false,hardwareIo:false,physicalReturnValidated:false,cpuReductionMeasured:false},
   maximumProductionJoinMilliseconds:500,fixtureCleanupOnlyMilliseconds:2000};
  write(path.join(root,'result.json'),result);console.log(JSON.stringify({id:result.id,status:result.status,profiles:result.profiles.map(p=>({label:p.label,generation:p.generation,passed:p.passed,failed:p.failed})),sourcesStable:true,artifactInputs:toolchain.length}));return result;
 }catch(error){write(path.join(root,'failure.json'),{status:'FAIL',error:error.message,profiles,steps,productMainExecuted:false,realSdkLoaded:false,hardwareIo:false});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===self){try{runSuite(parseArgs(process.argv.slice(2)));}catch(error){console.error(error.stack??error.message);process.exitCode=1;}}