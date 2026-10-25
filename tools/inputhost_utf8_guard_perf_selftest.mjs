#!/usr/bin/env node
// Standalone current-source UTF8 guard/full parser fixture. Production is read-only.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const rootDefault='G:/YeManCC-Work/Build/Validation/VirtualInputPerf-R5-20261015/host-utf8-guard';
const opts={};for(let i=2;i<process.argv.length;i+=2){if(!['--out','--rounds','--final','--prepare-only'].includes(process.argv[i])||!process.argv[i+1])throw Error('Bad argument');opts[process.argv[i].slice(2)]=process.argv[i+1];}
if(opts.final!==undefined&&!['true','false'].includes(opts.final))throw Error('--final must be true or false');
if(opts['prepare-only']!==undefined&&!['true','false'].includes(opts['prepare-only']))throw Error('--prepare-only must be true or false');
const finalMode=opts.final==='true',prepareOnly=opts['prepare-only']==='true';
const root=path.resolve(opts.out??rootDefault);fs.mkdirSync(root,{recursive:true});
const clock=new Date(),stamp=[clock.getFullYear(),clock.getMonth()+1,clock.getDate(),clock.getHours(),clock.getMinutes(),clock.getSeconds()].map((n,i)=>i?String(n).padStart(2,'0'):String(n)).join('');
const out=path.join(root,(finalMode?'final-':'run-')+stamp);fs.mkdirSync(out);
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const source=path.join(repo,'InputHost');
function walk(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()&&!['bin','obj'].includes(e.name)?walk(path.join(dir,e.name)):e.isFile()&&/\.(cs|csproj)$/.test(e.name)?[path.join(dir,e.name)]:[]);}
const files=walk(source);const snapshots=files.map(p=>({relative:path.relative(source,p),bytes:fs.readFileSync(p)}));
const host=path.join(out,'InputHost');fs.mkdirSync(host);
for(const f of snapshots){const dest=path.join(host,f.relative);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,f.bytes);}
const program=snapshots.find(f=>f.relative==='Program.cs').bytes.toString('utf8');
function closeBrace(text,start){let d=0,mode='code';for(let i=start;i<text.length;i++){const c=text[i],two=text.slice(i,i+2);if(mode==='line'){if(c==='\n')mode='code';}else if(mode==='block'){if(two==='*/'){mode='code';i++;}}else if(mode==='string'||mode==='char'){if(c==='\\')i++;else if(c===(mode==='string'?'"':"'"))mode='code';}else if(two==='//'){mode='line';i++;}else if(two==='/*'){mode='block';i++;}else if(c==='"')mode='string';else if(c==="'")mode='char';else if(c==='{')d++;else if(c==='}'&&--d===0)return i;}throw Error('Unclosed method');}
const marker='private static bool TryParseEnvelope(string message, out CommandEnvelope? envelope, out string error)';
const at=program.indexOf(marker);if(at<0||program.indexOf(marker,at+1)>=0)throw Error('Changed parser declaration');
const stop=closeBrace(program,program.indexOf('{',at))+1;
const method=program.slice(at,stop);
const before='string.IsNullOrWhiteSpace(message) || Encoding.UTF8.GetByteCount(message) > MaxMessageBytes';
const candidate='string.IsNullOrWhiteSpace(message) || (message.Length > MaxMessageBytes / 3 && Encoding.UTF8.GetByteCount(message) > MaxMessageBytes)';
// Canonical methods retain the production declaration; only the added helper is renamed.
function parserPlan(current,finalMode,baselineBefore=null,baselineCandidateSha=null){
 if(finalMode){
  if(current.split(candidate).length!==2)throw Error('Final mode requires the patched production guard');
  const restored=current.replace(candidate,before);
  if(restored!==baselineBefore)throw Error('Final parser differs from R5 baseline outside the guard; stop');
  if(sha(current.replace('TryParseEnvelope(','TryParseEnvelopeCandidate('))!==baselineCandidateSha)
   throw Error('Current production candidate does not match the R5 tested candidate');
  return {beforeCanonical:restored,candidateCanonical:current,
   extraMethod:restored.replace('TryParseEnvelope(','TryParseEnvelopeBefore('),
   beforeName:'TryParseEnvelopeBefore',candidateName:'TryParseEnvelope'};
 }
 if(current.split(before).length!==2)throw Error('Candidate mode requires the unpatched before guard');
 return {beforeCanonical:current,candidateCanonical:current.replace(before,candidate),
  extraMethod:current.replace(before,candidate).replace('TryParseEnvelope(','TryParseEnvelopeCandidate('),
  beforeName:'TryParseEnvelope',candidateName:'TryParseEnvelopeCandidate'};
}
let baselineBinding=null,baselineBefore=null,baselineCandidateSha=null;
if(finalMode){
 const baselineResultPath=path.join(root,'result.json');
 const bytes=fs.readFileSync(baselineResultPath),record=JSON.parse(bytes);
 if(record.manifest.guardBefore!==before||record.manifest.guardCandidate!==candidate)throw Error('Changed R5 baseline guard record');
 const baselineParserPath=path.join(record.runDirectory,'before-parser.cs');
 const baselineBytes=fs.readFileSync(baselineParserPath);baselineBefore=baselineBytes.toString('utf8');
 if(sha(baselineBytes)!==record.manifest.beforeParserSha256)throw Error('R5 before-parser bytes/hash changed');
 baselineCandidateSha=record.manifest.candidateParserSha256;
 baselineBinding={resultPath:baselineResultPath,resultSha256:sha(bytes),runDirectory:record.runDirectory,
  beforeParserPath:baselineParserPath,beforeParserSha256:record.manifest.beforeParserSha256,
  candidateParserSha256:baselineCandidateSha};
 fs.writeFileSync(path.join(out,'r5-before-parser.cs'),baselineBytes);
}
const plan=parserPlan(method,finalMode,baselineBefore,baselineCandidateSha);
const modified=plan.extraMethod;
const maxMatch=program.match(/private const int MaxMessageBytes = (.*);/);if(!maxMatch||maxMatch[1]!=='16 * 1024')throw Error('Changed byte limit');
fs.writeFileSync(path.join(out,'current-production-parser.cs'),method);
fs.writeFileSync(path.join(out,'before-parser.cs'),plan.beforeCanonical);
fs.writeFileSync(path.join(out,'candidate-parser.cs'),finalMode?method:modified);
fs.writeFileSync(path.join(out,'added-fixture-parser.cs'),modified);
const bridge=`using System;\nusing System.Collections.Generic;\nusing System.Reflection;\nusing System.Runtime.CompilerServices;\nusing System.Text;\nusing System.Text.Json;\nusing YeManInputHost.Diagnostics;\ninternal static partial class Program {\n${modified}\n
internal static bool FixtureParse(bool candidate,string? message) => candidate ? ${plan.candidateName}(message!,out _,out _) : ${plan.beforeName}(message!,out _,out _);
internal static string FixtureOutcome(bool candidate,string? message) {
    try {
        CommandEnvelope? envelope;string error;bool accepted;
        if(candidate)accepted=${plan.candidateName}(message!,out envelope,out error);else accepted=${plan.beforeName}(message!,out envelope,out error);
        var properties=new SortedDictionary<string,object?>(StringComparer.Ordinal);
        if(envelope is not null)foreach(var p in typeof(CommandEnvelope).GetProperties(BindingFlags.Instance|BindingFlags.Public)){
            var value=p.GetValue(envelope);properties[p.Name]=value;
            if(value is float f)properties[p.Name+"#IEEE32"]=BitConverter.SingleToInt32Bits(f).ToString("x8");
            if(value is double d)properties[p.Name+"#IEEE64"]=BitConverter.DoubleToInt64Bits(d).ToString("x16");
        }
        return JsonSerializer.Serialize(new{accepted,error,envelope=properties});
    }catch(Exception e){return JsonSerializer.Serialize(new{exception=e.GetType().FullName,message=e.Message});}
}
internal static string FixtureHash(int buttons,float lx,float ly,float rx,float ry,float lt,float rt)=>CanonicalFrameHash(buttons,lx,ly,rx,ry,lt,rt);
internal static int FixtureMax=>MaxMessageBytes;
}
internal static class ExtractedGuards {
 private const int MaxMessageBytes=${maxMatch[1]};
 [MethodImpl(MethodImplOptions.NoInlining)]internal static bool Before(string? message)=>${before};
 [MethodImpl(MethodImplOptions.NoInlining)]internal static bool Candidate(string? message)=>${candidate};
}
`;
fs.writeFileSync(path.join(host,'FixtureBridge.cs'),bridge);
const fixture=fs.readFileSync(path.join(repo,'tools/inputhost_utf8_guard_perf_selftest.cs'));fs.writeFileSync(path.join(host,'Utf8GuardFixture.cs'),fixture);
const csprojSource=snapshots.find(f=>f.relative==='YeManInputHost.csproj').bytes.toString('utf8');
let project=csprojSource.replace(/<HintPath>(.*?)<\/HintPath>/gs,(_,p)=>{
 const expanded=p.replace('$(MSBuildThisFileDirectory)',source+path.sep);return '<HintPath>'+path.resolve(source,expanded).replaceAll('&','&amp;')+'</HintPath>';
}).replace('<AssemblyName>YeManInputHost</AssemblyName>','<AssemblyName>InputHostUtf8GuardSelftest</AssemblyName>');
project=project.replace('<PropertyGroup>','<PropertyGroup>\n<StartupObject>Utf8GuardFixture</StartupObject>\n<NuGetAudit>false</NuGetAudit>\n<EnableNETAnalyzers>false</EnableNETAnalyzers>\n<TieredCompilation>true</TieredCompilation>\n<TieredPGO>true</TieredPGO>\n<RuntimeFrameworkVersion>10.0.11</RuntimeFrameworkVersion>\n<RollForward>Disable</RollForward>');
fs.writeFileSync(path.join(host,'YeManInputHost.csproj'),project);
const refPaths=[...project.matchAll(/<HintPath>(.*?)<\/HintPath>/gs)].map(x=>x[1].replaceAll('&amp;','&'));
const references=refPaths.map(p=>({path:p,sha256:sha(fs.readFileSync(p)),bytes:fs.statSync(p).size}));
const sourceManifest={productionModified:false,sourcePath:source,sourceFiles:snapshots.map(f=>({path:f.relative,sha256:sha(f.bytes),bytes:f.bytes.length})),
 mode:finalMode?'final-current-production':'candidate',benchmarkRequested:!finalMode,
 currentProductionParserSha256:sha(method),beforeParserSha256:sha(plan.beforeCanonical),
 candidateParserSha256:sha(finalMode?method:modified),candidateCanonicalSha256:sha(plan.candidateCanonical),
 addedParserSha256:sha(modified),fixtureBeforeCalls:plan.beforeName,fixtureCandidateCalls:plan.candidateName,
 candidateBranchIsCurrentProduction:finalMode,baselineBinding,guardBefore:before,guardCandidate:candidate,
 parserTailUnchanged:plan.beforeCanonical.replace(before,'GUARD')===plan.candidateCanonical.replace(candidate,'GUARD'),
 maxMessageBytes:16384,safeCodeUnits:5461,references,fixtureSha256:sha(fixture),runnerSha256:sha(fs.readFileSync(fileURLToPath(import.meta.url))),
 testChanges:'Copied project only: new StartupObject, copied fixture, extracted candidate method and private parser bridge. Original copied production .cs bytes unchanged.',
 forbiddenExecution:'Original Host Main, device/driver/SDK methods, pipe loop and lifecycle methods are never invoked.',
 staticInitialization:'Original data-only Program field initialization retained, including SDK type field metadata reflection. RuntimeSharedDependencies module initializer only registers its resolver.',
 runtimeRequested:'Microsoft.NETCore.App 10.0.11, RollForward Disable',completed:false};
fs.writeFileSync(path.join(out,'source-manifest.json'),JSON.stringify(sourceManifest,null,2));
if(prepareOnly){console.log('Prepared only: '+out);process.exit(0);}
function run(args,log,env=process.env,timeout=180000){const p=spawnSync('dotnet',args,{cwd:host,encoding:'utf8',env,timeout,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(out,log),(p.stdout??'')+(p.stderr??''));if(p.error||p.status!==0)throw Error('dotnet '+log+' failed: '+(p.error??p.status));return p.stdout;}
sourceManifest.dotnetInfo=run(['--info'],'dotnet-info.log');
const env={...process.env,DOTNET_TieredCompilation:'1',DOTNET_TieredPGO:'1',DOTNET_ReadyToRun:'1',DOTNET_CLI_TELEMETRY_OPTOUT:'1'};
const empty=path.join(out,'empty-nuget-source');fs.mkdirSync(empty);
run(['restore','YeManInputHost.csproj','--source',empty,'-p:NuGetAudit=false'],'restore.log');
const profiles=[];
for(const profile of ['Release','Debug']){
 run(['build','YeManInputHost.csproj','-c',profile,'--no-restore','--nologo'],'build-'+profile+'.log');
 console.log(profile+': copied actual Host parser compiled; only fixture entry will run');
 const dir=path.join(out,profile.toLowerCase());fs.mkdirSync(dir);
 const dll=path.join(host,'bin',profile,'net10.0-windows','InputHostUtf8GuardSelftest.dll');
 run([dll,dir,!finalMode&&profile==='Release'?'benchmark':'verify',opts.rounds??'7'],'run-'+profile+'.log',env,240000);
 const result=JSON.parse(fs.readFileSync(path.join(dir,'result.json'),'utf8'));profiles.push({profile,...result});
 console.log(profile+': '+result.functionalAssertions+' functional assertions; '+result.parserCaseCount+' full-parser cases');
}
sourceManifest.completed=true;sourceManifest.sourceStableDuringRun=snapshots.every(f=>sha(fs.readFileSync(path.join(source,f.relative)))===sha(f.bytes));
sourceManifest.referencesStableDuringRun=references.every(f=>sha(fs.readFileSync(f.path))===f.sha256);
fs.writeFileSync(path.join(out,'source-manifest.json'),JSON.stringify(sourceManifest,null,2));
const result={ok:true,mode:sourceManifest.mode,productionModified:false,benchmarkExecuted:!finalMode,runDirectory:out,manifest:sourceManifest,profiles};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));fs.writeFileSync(path.join(root,finalMode?'final-result.json':'result.json'),JSON.stringify(result,null,2));
if(!sourceManifest.sourceStableDuringRun||!sourceManifest.referencesStableDuringRun)throw Error('Source/reference changed; keep snapshot, do not claim final binding');
console.log('Evidence: '+out);
