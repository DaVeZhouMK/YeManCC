// Pure current-source parser A/B. Never builds or runs Host/HostSession/SDK/device code.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2), opts={};
for(let i=0;i<args.length;i+=2){if(!['--out','--mode'].includes(args[i])||!args[i+1])throw Error('Expected --out/--mode value');opts[args[i].slice(2)]=args[i+1];}
const mode=opts.mode??'candidate';if(!['candidate','final'].includes(mode))throw Error('Bad mode');
const root=path.resolve(opts.out??'G:/YeManCC-Work/Build/Validation/VirtualInput-Stability-20261018/HostCost');
const out=path.join(root,mode+'-'+new Date().toISOString().replace(/[:.]/g,'-'));
fs.mkdirSync(out,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const save=(name,value)=>fs.writeFileSync(path.join(out,name),JSON.stringify(value,null,2)+'\n');
const files=['Program.cs','ScreenTouchpadReport.cs','ScreenButtonReport.cs','CanonicalFrameFingerprintMemo.cs','MsiVirtualReportChain1410.cs','MsiSteamDeckReleaseFence1726.cs'];
const snapshots=Object.fromEntries(files.map(p=>[p,fs.readFileSync(path.join(repo,'InputHost',p))]));
fs.mkdirSync(path.join(out,'snapshot'));
for(const [name,bytes] of Object.entries(snapshots))fs.writeFileSync(path.join(out,'snapshot',name),bytes);
const source=snapshots['Program.cs'].toString('utf8').replace(/^\uFEFF/,'');
function block(text,start){let depth=0,state='code';for(let i=start;i<text.length;i++){const c=text[i],two=text.slice(i,i+2);if(state==='line'){if(c==='\n')state='code';}else if(state==='comment'){if(two==='*/'){state='code';i++;}}else if(state==='string'||state==='char'){if(c==='\\')i++;else if(c===(state==='string'?'"':"'"))state='code';}else if(two==='//'){state='line';i++;}else if(two==='/*'){state='comment';i++;}else if(c==='"')state='string';else if(c==="'")state='char';else if(c==='{')depth++;else if(c==='}'&&--depth===0)return i+1;}throw Error('Unclosed source block');}
function extract(text,marker){const start=text.indexOf(marker);if(start<0||text.indexOf(marker,start+marker.length)>=0)throw Error('Nonunique marker '+marker);let end=block(text,text.indexOf('{',start));if(text[end]===';')end++;return text.slice(start,end);}
function method(text,name){const matches=[...text.matchAll(new RegExp('^    private static [^\\r\\n]*\\b'+name+'\\(','gm'))];if(matches.length!==1)throw Error('Method marker '+name);return extract(text,matches[0][0].trim());}
function match(text,re){const x=text.match(re);if(!x)throw Error('Missing '+re);return x[0];}
const before='!(value == 0f && element.GetRawText().StartsWith("-", StringComparison.Ordinal))';
const after='BitConverter.SingleToInt32Bits(value) != int.MinValue';
const names=['TryParseEnvelope','AttachReportChainMetadata1410Safely','TryReadString','TryReadUInt64','TryReadInt32','TryReadShort','TryReadByte','TryReadAxis','TryReadTrigger','TryReadImuAxis','TryReadImuQuaternion','IsSha256','CanonicalFrameHash','AppendFrameHashBits'];
const methods=Object.fromEntries(names.map(n=>[n,method(source,n)]));
const variants={Baseline:{...methods},Candidate:{...methods}};
for(const n of ['TryReadAxis','TryReadTrigger']){
 const actual=methods[n],needle=mode==='candidate'?before:after;
 if(actual.split(needle).length!==2)throw Error('Changed signed-zero predicate '+n);
 variants.Baseline[n]=mode==='candidate'?actual:actual.replace(after,before);
 variants.Candidate[n]=mode==='candidate'?actual.replace(before,after):actual;
 if(variants.Candidate[n].replace(after,before)!==variants.Baseline[n])throw Error('Nonlocal candidate change');
}
const fields=['BaseFields','FrameFields','HidHideFields'].map(n=>extract(source,'private static readonly HashSet<string> '+n+' =')).join('\n');
const constants=['ProtocolVersion','MaxMessageBytes'].map(n=>match(source,new RegExp('private const int '+n+'\\s*=\\s*[^;]+;'))).join('\n');
const pads=snapshots['ScreenTouchpadReport.cs'].toString('utf8');
const buttons=snapshots['ScreenButtonReport.cs'].toString('utf8');
const envelope=extract(source,'private sealed record CommandEnvelope(').replace('private sealed record','internal sealed record');
const pad=match(pads,/private readonly record struct ScreenPad\([^;]+;/).replace('private readonly','internal readonly');
const cache=match(source,/\[ThreadStatic\] private static Dictionary<string, JsonElement>\? t_fieldCache;/);
const memo=extract(snapshots['CanonicalFrameFingerprintMemo.cs'].toString('utf8'),'internal static class CanonicalFrameFingerprintMemo').replace('internal static class','private static class');
const tail=['TryReadScreenPads','TryReadScreenPad'].map(n=>method(pads,n)).join('\n')+'\n'+method(buttons,'TryReadScreenButtons');
let generated='using System;\nusing System.Collections.Generic;\nusing System.Text;\nusing System.Text.Json;\nusing System.Security.Cryptography;\nusing System.Runtime.CompilerServices;\nusing YeManInputHost.Diagnostics;\ninternal static class GeneratedParsers {\n'+envelope+'\n'+pad+'\n';
for(const [name,pieces] of Object.entries(variants))generated+='internal static class '+name+' {\n'+constants+'\n'+fields+'\n'+cache+'\n'+Object.values(pieces).join('\n')+'\n'+tail+'\n'+memo+`\ninternal static bool Parse(string s,out CommandEnvelope? e,out string error)=>TryParseEnvelope(s,out e,out error);
internal static string Hash(int b,float x,float y,float z,float w,float l,float r)=>CanonicalFrameHash(b,x,y,z,w,l,r);
internal static bool Axis(IReadOnlyDictionary<string,JsonElement> p,out float f)=>TryReadAxis(p,"v",out f);
internal static bool Trigger(IReadOnlyDictionary<string,JsonElement> p,out float f)=>TryReadTrigger(p,"v",out f);
}\n`;
const fence=match(snapshots['MsiSteamDeckReleaseFence1726.cs'].toString('utf8'),/public const string PrepareField\s*=\s*[^;]+;/);
generated+='}\ninternal static class MsiSteamDeckReleaseFence1726 { '+fence+' }\n';
fs.writeFileSync(path.join(out,'GeneratedParsers.cs'),generated);
fs.writeFileSync(path.join(out,'ActualMetadata.cs'),snapshots['MsiVirtualReportChain1410.cs']);
const fixture=fs.readFileSync(path.join(repo,'tools/inputhost_zero_number_cost_perf_selftest.cs'));
fs.writeFileSync(path.join(out,'Fixture.cs'),fixture);
const project='<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems><NuGetAudit>false</NuGetAudit><TreatWarningsAsErrors>true</TreatWarningsAsErrors><RuntimeFrameworkVersion>10.0.11</RuntimeFrameworkVersion><RollForward>Disable</RollForward></PropertyGroup><ItemGroup><Compile Include="Fixture.cs"/><Compile Include="GeneratedParsers.cs"/><Compile Include="ActualMetadata.cs"/></ItemGroup></Project>\n';
fs.writeFileSync(path.join(out,'HostZeroCost.csproj'),project);
const plan={rounds:11,minimumFasterRounds:9,minimumStableReduction:.03,maximumOtherRegression:.05,callsPerSample:16384,allocationCalls:4096,profiles:['Release','Debug'],timing:['Release-no-tiering','Release-tiered-PGO'],scope:'Complete current-source pure parser plus exact number readers; no Host/HostSession constructors, lifecycle, SDK, pipe, device, service or driver calls. One formal campaign per runtime mode; no timing retries.',adoption:'All semantic comparisons pass; both stable-zero rows improve >=3% with >=9/11 faster pairs in both runtime modes; all controls <=5% regression; no row allocates more; stable allocation decreases.'};
const manifest={mode,sourceFiles:Object.entries(snapshots).map(([p,b])=>({path:'InputHost/'+p,sha256:sha(b),bytes:b.length})),before,after,methods:Object.fromEntries(names.map(n=>[n,{baselineSha256:sha(variants.Baseline[n]),candidateSha256:sha(variants.Candidate[n])}])),onlyChangedMethods:['TryReadAxis','TryReadTrigger'],generatedSha256:sha(generated),fixtureSha256:sha(fixture),runnerSha256:sha(fs.readFileSync(fileURLToPath(import.meta.url))),actualProductionCandidate:mode==='final',SDKLinked:false,originalProgramTypePresent:false,plan};
save('source-manifest.json',manifest);save('acceptance-plan.json',plan);
function run(args,log,env={}){const p=spawnSync('dotnet',args,{cwd:out,encoding:'utf8',timeout:240000,maxBuffer:8*1024*1024,env:{...process.env,DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE:'true',...env}});fs.writeFileSync(path.join(out,log),(p.stdout??'')+(p.stderr??''));if(p.error||p.status!==0)throw Error(log+' '+(p.error??p.status)+'\n'+(p.stdout??'')+(p.stderr??''));}
const empty=path.join(out,'empty-nuget-source');fs.mkdirSync(empty);
run(['--info'],'dotnet-info.log');
run(['restore','HostZeroCost.csproj','--source',empty,'-p:NuGetAudit=false'],'restore.log');
const results=[];
for(const profile of ['Release','Debug']){
 run(['build','HostZeroCost.csproj','-c',profile,'--no-restore','--nologo'],'build-'+profile+'.log');
 const settings=profile==='Release'&&mode==='candidate'?[['no-tiering',{DOTNET_TieredCompilation:'0'}],['tiered-PGO',{DOTNET_TieredCompilation:'1',DOTNET_TieredPGO:'1'}]]:[['verify',{}]];
 for(const [setting,env] of settings){const result=path.join(out,profile+'-'+setting+'.json');run([path.join(out,'bin',profile,'net10.0','HostZeroCost.dll'),result,setting==='verify'?'verify':'benchmark'],'run-'+profile+'-'+setting+'.log',env);const data=JSON.parse(fs.readFileSync(result));if(!data.pass)throw Error('Semantic failure');results.push({profile,setting,...data});console.log(profile+' '+setting+': '+data.checks+' checks; performance gate '+data.performancePass);}
}
manifest.sourceStableDuringRun=Object.entries(snapshots).every(([p,b])=>sha(fs.readFileSync(path.join(repo,'InputHost',p)))===sha(b));
if(!manifest.sourceStableDuringRun)throw Error('Source drift');
if(mode==='final'){
 const prior=JSON.parse(fs.readFileSync(path.join(root,'candidate-result.json')));
 manifest.candidateBindingMatches=JSON.stringify(prior.manifest.methods)===JSON.stringify(manifest.methods);
 if(!manifest.candidateBindingMatches)throw Error('Candidate method drift from measured campaign');
}
const result={pass:true,performancePass:results.every(x=>x.performancePass!==false),runDirectory:out,manifest,results};save('source-manifest.json',manifest);save('result.json',result);fs.writeFileSync(path.join(root,mode+'-result.json'),JSON.stringify(result,null,2)+'\n');console.log(out);
