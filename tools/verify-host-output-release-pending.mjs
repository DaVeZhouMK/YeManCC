// Independent R103 regression: real lifecycle members and selected dispatcher
// clauses; inert SDK/PnP/output guard/diagnostic/clock boundaries. No Host build.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const self=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(self),'..'),workspace=path.resolve(repo,'../../..');
const root=path.join(workspace,'Build/Validation/VirtualInput-BugPriority-R103-20261016/output-stop-integration');
let out=root,expected='685ddad85a789cdabad4ee8adb3ff091557721a8bb856d33c014ae9d72085da8';
for(let i=2;i<process.argv.length;i++){
 if(process.argv[i]==='--out')out=path.resolve(process.argv[++i]);
 else if(process.argv[i]==='--expected-sha')expected=process.argv[++i];
 else throw Error('Usage: node verify-host-output-release-pending.mjs [--out DIR] [--expected-sha ProgramSHA256]');
}
const rel=path.relative(root,out);if(rel.startsWith('..')||path.isAbsolute(rel))throw Error('Output must stay in R103/output-stop-integration.');
fs.mkdirSync(out,{recursive:true});const run=path.join(out,`run-${new Date().toISOString().replace(/[:.]/g,'')}-${process.pid}`);fs.mkdirSync(run);
const hash=s=>crypto.createHash('sha256').update(s).digest('hex');
const json=(name,value)=>fs.writeFileSync(path.join(run,name),JSON.stringify(value,null,2)+'\n');
function read(file){const b=fs.statSync(file),data=fs.readFileSync(file),a=fs.statSync(file);if(b.size!==a.size||b.mtimeMs!==a.mtimeMs)throw Error('Input changed: '+file);return{file,data,text:data.toString('utf8'),sha256:hash(data)};}
const source=read(path.join(repo,'InputHost/Program.cs'));
if(source.sha256!==expected.toLowerCase())throw Error(`Program SHA mismatch: expected ${expected}, found ${source.sha256}; choose the intended snapshot explicitly.`);
const template=read(path.join(repo,'tools/fixtures/host_output_release_pending.cs'));
const timing=read(path.join(repo,'InputHost/HostReleaseTimingEvidence.cs'));
const inputs=[source,template,timing,read(self)];
json('source-bindings-before.json',inputs.map(({data,text,...r})=>r));
fs.writeFileSync(path.join(run,'Program.cs.snapshot'),source.data);
fs.writeFileSync(path.join(run,'Timing.cs.snapshot'),timing.data);
const masked=s=>s.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|@"(?:""|[^"])*"|"(?:\\[\s\S]|[^"\\])*"|'(?:\\[\s\S]|[^'\\])*'/g,m=>m.replace(/[^\r\n]/g,' '));
const receipts=[];
function range(s,name,start,end,kind){const code=s.text.slice(start,end);const item={name,kind,source:s.file,sourceSha256:s.sha256,startLine:s.text.slice(0,start).split('\n').length,endLine:s.text.slice(0,end).split('\n').length,startUtf16:start,endUtf16:end,extractedSha256:hash(code),code};receipts.push(item);return code;}
function balanced(s,start,left,right){let depth=0;for(let i=start;i<s.length;i++){if(s[i]===left)++depth;else if(s[i]===right&&--depth===0)return i+1;}throw Error('Unbalanced extraction');}
function method(s,name){
 const escaped=name.replace(/[.*+?^${}()|[\]\\]/g,'\\$&');
 const rx=new RegExp('^[ \\t]*(?:private|public|internal) (?:static )?[^\\r\\n;{}=]*\\b'+escaped+'\\s*\\(','gm');
 const matches=[...s.text.matchAll(rx)];if(matches.length!==1)throw Error(`Expected one ${name}; got ${matches.length}`);
 const start=matches[0].index,m=masked(s.text),paren=m.indexOf('(',start);let p=balanced(m,paren,'(',')');while(/\s/.test(m[p]??''))++p;
 const end=m.slice(p,p+2)==='=>'?m.indexOf(';',p)+1:balanced(m,p,'{','}');
 return range(s,name,start,end,'method');
}
function type(s,name,decl){const rx=new RegExp('^[ \\t]*'+decl+' '+name+'\\b','m'),m=rx.exec(s.text);if(!m)throw Error('Missing '+name);const mask=masked(s.text),open=mask.indexOf('{',m.index);return range(s,name,m.index,balanced(mask,open,'{','}'),'type');}
function statement(s,name,rx){const m=rx.exec(s.text);if(!m)throw Error('Missing '+name);return range(s,name,m.index,m.index+m[0].length,'statement');}
const substitutions=[];
function clock(code,name){const changed=code.replaceAll('Environment.TickCount64','Fixture.Now').replaceAll('Thread.Sleep','Fixture.Sleep');if(changed!==code)substitutions.push({name,changes:['Environment.TickCount64 -> Fixture.Now','Thread.Sleep -> Fixture.Sleep'],kind:'inert clock/sleep only'});return changed;}
const methods=['ReleaseLocal','TryStopSteamDeckIdleForRelease','TryStopControllerOutputForRelease','TryCompleteIdleCtsRelease','ReleaseLocalLocked','TryPrepare','TryGetVirtualOutputProfile','RememberIdempotent'];
const lifecycle=methods.map(n=>clock(method(source,n),n)).join('\n');
const wholeHandle=method(source,'HandleLocked'),handleStart=source.text.indexOf(wholeHandle);
function part(name,from,to){const a=wholeHandle.indexOf(from),b=wholeHandle.indexOf(to,a);if(a<0||b<=a)throw Error('Missing dispatcher '+name);return range(source,name,handleStart+a,handleStart+b,'dispatcher-clause');}
const bodyStart=wholeHandle.indexOf('{')+1;
const helloStart=wholeHandle.indexOf('            if (_phase == Phase.AwaitHello)');
if(helloStart<bodyStart)throw Error('Missing HELLO boundary');
const prologue=range(source,'HandleLocked.prologue-and-request-replay',handleStart+bodyStart,handleStart+helloStart,'dispatcher-clause');
const validation=part('HandleLocked.tuple-and-sequence-validation','            if (envelope!.Command == "HELLO")','            switch (envelope.Command)');
const cases=part('HandleLocked.prepare-recreate-submit-quiesce-release-shutdown','                case "PREPARE_TARGET":','                case "HIDHIDE_CYCLE_BEGIN":');
if((cases.match(/_phase = LocalReleasePending \?/g)??[]).length!==5||!cases.includes('shutdown = !LocalReleasePending;'))throw Error('Expected five pending phase decisions and shutdown gate.');
const prop=statement(source,'LocalReleasePending',/^        private bool LocalReleasePending =>[^\r\n]+;/m);
statement(source,'_outputReleasePending',/^        private bool _outputReleasePending;/m);
if(!method(source,'TryPrepare').includes('_outputReleasePending = false;'))throw Error('Successful create reset missing.');
const phase=type(source,'Phase','private enum');
const obs=type(source,'VirtualEndpointObservation','private readonly record struct');
const tuple=method(source,'SameTuple');
const idem=statement(source,'IdempotentReply',/^    private sealed record IdempotentReply[^\r\n]+;/m);
const timingEnum=type(timing,'Stage','internal enum');
const timingName=method(timing,'Name');
if(!/Stage\.OutputReaderStop\s*=>\s*"outputReaderStop"/.test(timingName)||!/StageCount = 13;/.test(timing.text))throw Error('Timing outputReaderStop / fixed 13-stage schema missing.');
const replacements={'@@PHASE@@':phase,'@@OBSERVATION@@':obs,'@@SAME_TUPLE@@':tuple,'@@IDEMPOTENT@@':idem,'@@PENDING_PROPERTY@@':prop,
 '@@TIMING_ENUM@@':timingEnum,'@@WAIT@@':clock(method(source,'WaitForVirtualEndpointRemoval'),'WaitForVirtualEndpointRemoval'),
 '@@LIFECYCLE_METHODS@@':lifecycle,'@@DISPATCH_PROLOGUE@@':clock(prologue,'HandleLocked.prologue'),'@@DISPATCH_VALIDATION@@':clock(validation,'HandleLocked.validation'),'@@DISPATCH_CASES@@':cases};
let code=template.text;for(const[token,value]of Object.entries(replacements)){if(code.split(token).length!==2)throw Error('Template token mismatch '+token);code=code.replace(token,()=>value);}
if(/@@[A-Z_]+@@/.test(code))throw Error('Unfilled fixture token');
fs.writeFileSync(path.join(run,'ExtractedFixture.cs'),code);
json('extracted-source-receipt.json',{sourceSha256:source.sha256,members:receipts,substitutions,standIns:['SDK controller/context/profile factory and output guard','PnP, MSI fence and diagnostics','envelope JSON parsing/shape and response formatting','cost accounting and clocks'],notTested:['initial HELLO negotiation','native recovery state machine','real SDK output-stop implementation','physical driver/consumer behavior'],productionHostRun:false});
fs.writeFileSync(path.join(run,'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>\n');
const project=path.join(run,'OutputReleasePendingSelfTest.csproj');
fs.writeFileSync(project,`<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><Nullable>enable</Nullable><ImplicitUsings>disable</ImplicitUsings><TreatWarningsAsErrors>true</TreatWarningsAsErrors><EnableNETAnalyzers>false</EnableNETAnalyzers><EnableDefaultCompileItems>false</EnableDefaultCompileItems><UseAppHost>false</UseAppHost><NuGetAudit>false</NuGetAudit><RestoreSources></RestoreSources><AssemblyName>HostOutputReleasePendingSelfTest</AssemblyName></PropertyGroup><ItemGroup><Compile Include="ExtractedFixture.cs"/></ItemGroup></Project>\n`);
const commands=[];
function execute(name,args){const r=spawnSync(process.env.DOTNET_EXE??'dotnet',args,{cwd:run,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:16*1024*1024,env:{...process.env,DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_NOLOGO:'1',DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE:'true'}});fs.writeFileSync(path.join(run,name+'.stdout.log'),r.stdout??'');fs.writeFileSync(path.join(run,name+'.stderr.log'),r.stderr??'');commands.push({name,args,status:r.status,error:r.error?.message??null});json('commands.json',commands);if(r.error||r.status!==0)throw Error(name+' failed; '+path.join(run,name+'.stdout.log'));return r.stdout;}
let results,failure;
try{execute('restore',['restore',project,'--configfile',path.join(run,'NuGet.Config'),'--nologo','--verbosity','minimal']);execute('build',['build',project,'--configuration','Release','--no-restore','--nologo','--verbosity','minimal']);results=JSON.parse(execute('selftest',[path.join(run,'bin/Release/net10.0/HostOutputReleasePendingSelfTest.dll')]));json('selftest-results.json',results);if(results.failed!==0)throw Error('Fixture cases failed.');}
catch(error){failure=String(error?.message??error);const p=path.join(run,'selftest.stdout.log');if(fs.existsSync(p)){try{results=JSON.parse(fs.readFileSync(p,'utf8'));json('selftest-results.json',results);}catch{}}}
const after=inputs.map(s=>({file:s.file,sha256:hash(fs.readFileSync(s.file)),unchanged:hash(fs.readFileSync(s.file))===s.sha256}));json('source-bindings-after.json',after);
if(!after.every(r=>r.unchanged))failure??='An input changed; rerun against the intended snapshot.';
const summary={version:1,status:failure?'fail':'pass',failure:failure??null,runDirectory:run,programSha256:source.sha256,sourceStable:after.every(r=>r.unchanged),passed:results?.passed??null,failed:results?.failed??null,actualLifecycleMethods:methods,actualDispatcherPhaseClauses:5,timingOutputReaderStopSchema:true,warningsAsErrors:true,productionHostBuiltOrRun:false,realSDKOrThreadOrDeviceRun:false};json('validation-summary.json',summary);fs.writeFileSync(path.join(out,'latest-validation.json'),JSON.stringify(summary,null,2)+'\n');console.log(JSON.stringify(summary,null,2));process.exitCode=failure?1:0;