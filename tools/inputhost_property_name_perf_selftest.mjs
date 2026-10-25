#!/usr/bin/env node
// R6 candidate only. Copy the actual Host project; never execute its Main or device methods.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const rootDefault='G:/YeManCC-Work/Build/Validation/VirtualInputPerf-R6-20261015/parser-property-names';
const baseline='G:/YeManCC-Work/Build/Validation/VirtualInputPerf-R6-20261015/before/InputHost';
const expected='b0477a7448945f8e2b7d706a3c1df9488fce3ffdf76b295435183f16a995312d';
const opts={};for(let i=2;i<process.argv.length;i+=2){if(!['--out','--rounds','--prepare-only','--source'].includes(process.argv[i])||!process.argv[i+1])throw Error('Bad argument');opts[process.argv[i].slice(2)]=process.argv[i+1];}
if(opts['prepare-only']!==undefined&&!['true','false'].includes(opts['prepare-only']))throw Error('prepare-only must be true or false');
const rounds=Number(opts.rounds??7);if(!Number.isInteger(rounds)||rounds<3||rounds>11)throw Error('rounds outside 3..11');
const root=path.resolve(opts.out??rootDefault);fs.mkdirSync(root,{recursive:true});
const date=new Date(),stamp=[date.getFullYear(),date.getMonth()+1,date.getDate(),date.getHours(),date.getMinutes(),date.getSeconds()].map((v,i)=>i?String(v).padStart(2,'0'):String(v)).join('');
const out=path.join(root,'run-'+stamp);fs.mkdirSync(out);
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const liveSource=path.join(repo,'InputHost');
const sourceMode=opts.source??'current';if(!['current','before'].includes(sourceMode))throw Error('--source must be current or before');
const source=sourceMode==='before'?baseline:liveSource;
function walk(dir){return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.isDirectory()&&!['bin','obj'].includes(e.name)?walk(path.join(dir,e.name)):e.isFile()&&/\.(cs|csproj)$/.test(e.name)?[path.join(dir,e.name)]:[]);}
const snapshots=walk(source).map(p=>({relative:path.relative(source,p),bytes:fs.readFileSync(p)}));
const beforeSnapshots=walk(baseline).map(p=>({relative:path.relative(baseline,p),bytes:fs.readFileSync(p)}));
const programBytes=snapshots.find(f=>f.relative==='Program.cs').bytes,program=programBytes.toString('utf8');
if(sha(fs.readFileSync(path.join(baseline,'Program.cs')))!==expected)throw Error('R6 before Program source binding changed');
const approvedCapacityEdits=(program.match(/new Dictionary<HMAxis, float>\(6\)/g)??[]).length;
const normalizedProgram=program.replaceAll('new Dictionary<HMAxis, float>(6)','new Dictionary<HMAxis, float>');
if(sha(programBytes)!==expected && (approvedCapacityEdits!==2 || sha(Buffer.from(normalizedProgram,'utf8'))!==expected))throw Error('Current Program differs from locked before beyond the two parent-approved Axes capacities');
if(snapshots.length!==beforeSnapshots.length||snapshots.some(f=>!beforeSnapshots.some(b=>b.relative===f.relative&&(f.relative==='Program.cs'||sha(b.bytes)===sha(f.bytes)))))throw Error('R6 before/InputHost differs from current .cs/.csproj snapshot');
const host=path.join(out,'InputHost');fs.mkdirSync(host);
for(const f of snapshots){const dest=path.join(host,f.relative);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,f.bytes);}
function closeBrace(text,start){let d=0,mode='code';for(let i=start;i<text.length;i++){const c=text[i],two=text.slice(i,i+2);if(mode==='line'){if(c==='\n')mode='code';}else if(mode==='block'){if(two==='*/'){mode='code';i++;}}else if(mode==='string'||mode==='char'){if(c==='\\')i++;else if(c===(mode==='string'?'"':"'"))mode='code';}else if(two==='//'){mode='line';i++;}else if(two==='/*'){mode='block';i++;}else if(c==='"')mode='string';else if(c==="'")mode='char';else if(c==='{')d++;else if(c==='}'&&--d===0)return i;}throw Error('Unclosed method');}
const marker='private static bool TryParseEnvelope(string message, out CommandEnvelope? envelope, out string error)';
const at=program.indexOf(marker);if(at<0||program.indexOf(marker,at+1)>=0)throw Error('Changed parser declaration');
const method=program.slice(at,closeBrace(program,program.indexOf('{',at))+1);
const loopMarker='            foreach (var property in root.EnumerateObject())';
const start=method.indexOf(loopMarker),end=closeBrace(method,method.indexOf('{',start))+1;
if(start<0||method.indexOf(loopMarker,start+1)>=0)throw Error('Property loop changed');
const originalLoop=method.slice(start,end);
if((originalLoop.match(/property.Name/g)??[]).length!==4)throw Error('Unexpected original name consumers');
const loop=`            if (t_propertyNameCooldown > 0)
            {
                t_propertyNameCooldown--;
${originalLoop}
            }
            else
            {
                var propertyNameHints = t_propertyNameHints ??= new string?[64];
                var propertyIndex = 0;
                var propertyMisses = 0;
                foreach (var property in root.EnumerateObject())
                {
                    var propertyName = CandidatePropertyName(property, propertyNameHints, propertyIndex++, ref propertyMisses);
                    if (!properties.TryAdd(propertyName, property.Value))
                    {
                        if (propertyName == MsiReportChainProtocol1410.PrepareField) { prepareMetadataDuplicate1410 = true; continue; }
                        if (propertyName == MsiReportChainProtocol1410.SourceField) { sourceMetadataDuplicate1410 = true; continue; }
                        CandidateFinishPropertyHints(propertyIndex, propertyMisses);
                        error = "duplicate-field:" + propertyName; return false;
                    }
                }
                CandidateFinishPropertyHints(propertyIndex, propertyMisses);
            }`;
const liveProgramBytes=fs.readFileSync(path.join(liveSource,'Program.cs'));
if(!liveProgramBytes.toString('utf8').includes(method))throw Error('Live production parser differs from locked R6 baseline');
const candidateCanonical=method.slice(0,start)+loop+method.slice(end);
const candidate=candidateCanonical.replace('TryParseEnvelope(','TryParseEnvelopeCandidate(');
fs.writeFileSync(path.join(out,'before-parser.cs'),method);fs.writeFileSync(path.join(out,'candidate-parser.cs'),candidate);
fs.writeFileSync(path.join(out,'original-property-loop.cs'),originalLoop);fs.writeFileSync(path.join(out,'candidate-property-loop.cs'),loop);
const helper=`    [ThreadStatic] private static string?[]? t_propertyNameHints;
    [ThreadStatic] private static int t_propertyNameCooldown;
    private static void CandidateFinishPropertyHints(int count, int misses)
    {
        if (count > 0 && misses * 2 >= Math.Min(count, 64)) t_propertyNameCooldown = 8;
    }
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static string CandidatePropertyName(JsonProperty property, string?[] hints, int position, ref int misses)
    {
        if ((uint)position >= (uint)hints.Length) return property.Name;
        var hint = hints[position];
        if (hint is not null && property.NameEquals(hint)) return hint;
        misses++;
        var decoded = property.Name;
        hints[position] = CandidateCanonicalKey(decoded);
        return decoded;
    }
    private static string? CandidateCanonicalKey(string decoded)
    {
        if (FrameFields.TryGetValue(decoded, out var canonical)) return canonical;
        if (HidHideFields.TryGetValue(decoded, out canonical)) return canonical;
        if (decoded == MsiReportChainProtocol1410.PrepareField) return MsiReportChainProtocol1410.PrepareField;
        if (decoded == MsiReportChainProtocol1410.SourceField) return MsiReportChainProtocol1410.SourceField;
        if (decoded == MsiSteamDeckReleaseFence1726.PrepareField) return MsiSteamDeckReleaseFence1726.PrepareField;
        return null;
    }
`;
fs.writeFileSync(path.join(out,'candidate-helper.cs'),helper);
const plainRoot=path.join(rootDefault,'run-20261015014248');
const plainManifest=JSON.parse(fs.readFileSync(path.join(plainRoot,'source-manifest.json'),'utf8'));
const plainParserBytes=fs.readFileSync(path.join(plainRoot,'candidate-parser.cs')),plainHelperBytes=fs.readFileSync(path.join(plainRoot,'candidate-helper.cs'));
if(sha(plainParserBytes)!==plainManifest.candidateParserSha256||sha(plainHelperBytes)!==plainManifest.candidateHelperSha256||sha(method)!==plainManifest.beforeParserSha256)throw Error('Plain candidate control binding changed');
const renamePlain=x=>x.replaceAll('TryParseEnvelopeCandidate','TryParseEnvelopePlainCandidate').replaceAll('t_propertyNameHints','t_plainPropertyNameHints').replaceAll('CandidatePropertyName','PlainPropertyName').replaceAll('CandidateCanonicalKey','PlainCanonicalKey');
const plainParser=renamePlain(plainParserBytes.toString('utf8')),plainHelper=renamePlain(plainHelperBytes.toString('utf8'));
fs.writeFileSync(path.join(out,'plain-control-parser.cs'),plainParser);fs.writeFileSync(path.join(out,'plain-control-helper.cs'),plainHelper);

const bridge=`using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using YeManInputHost.Diagnostics;
internal static partial class Program {
${helper}
${candidate}
${plainHelper}
${plainParser}
internal static bool FixtureParseMode(int mode,string? message) => mode switch {
  0=>TryParseEnvelope(message!,out _,out _), 1=>TryParseEnvelopePlainCandidate(message!,out _,out _),
  2=>TryParseEnvelopeCandidate(message!,out _,out _), _=>throw new ArgumentOutOfRangeException(nameof(mode))};
internal static bool FixtureParse(bool candidate,string? message) => candidate ? TryParseEnvelopeCandidate(message!,out _,out _) : TryParseEnvelope(message!,out _,out _);
internal static string FixtureOutcome(bool candidate,string? message) {
    try {
        CommandEnvelope? envelope;string error;bool accepted;
        if(candidate)accepted=TryParseEnvelopeCandidate(message!,out envelope,out error);else accepted=TryParseEnvelope(message!,out envelope,out error);
        return JsonSerializer.Serialize(new{accepted,error,envelope=FixtureFingerprint(envelope)});
    }catch(Exception e){return JsonSerializer.Serialize(new{exception=e.GetType().FullName,message=e.Message});}
}
private static object? FixtureFingerprint(object? value) {
    if(value is null || value is string || value is bool) return value;
    if(value is float f) return new{type="float",bits=BitConverter.SingleToInt32Bits(f).ToString("x8")};
    if(value is double d) return new{type="double",bits=BitConverter.DoubleToInt64Bits(d).ToString("x16")};
    var type=value.GetType();if(type.IsPrimitive || type.IsEnum || value is decimal)return value;
    if(value is IEnumerable sequence) {var list=new List<object?>();foreach(var item in sequence)list.Add(FixtureFingerprint(item));return list;}
    var result=new SortedDictionary<string,object?>(StringComparer.Ordinal);
    result["#type"]=type.FullName;
    foreach(var p in type.GetProperties(BindingFlags.Instance|BindingFlags.Public))if(p.GetIndexParameters().Length==0)result[p.Name]=FixtureFingerprint(p.GetValue(value));
    foreach(var field in type.GetFields(BindingFlags.Instance|BindingFlags.Public))result[field.Name]=FixtureFingerprint(field.GetValue(value));
    return result;
}
internal static string FixtureHash(int buttons,float lx,float ly,float rx,float ry,float lt,float rt)=>CanonicalFrameHash(buttons,lx,ly,rx,ry,lt,rt);
internal static int FixtureMax=>MaxMessageBytes;
internal static string?[]? FixtureHints()=>t_propertyNameHints;
internal static bool FixtureHintsOwned()=>t_propertyNameHints is null || (t_propertyNameHints.Length==64 && Array.TrueForAll(t_propertyNameHints,h=>h is null || ReferenceEquals(h,CandidateCanonicalKey(h))));
internal static int FixtureCooldown()=>t_propertyNameCooldown;
internal static void FixtureForceProbe()=>t_propertyNameCooldown=0;
internal static void FixtureResetHints(){t_propertyNameHints=null;t_plainPropertyNameHints=null;t_propertyNameCooldown=0;}
internal static string[] FixtureKnownKeys()=>FrameFields.Concat(HidHideFields).Concat(new[]{MsiReportChainProtocol1410.PrepareField,MsiReportChainProtocol1410.SourceField,MsiSteamDeckReleaseFence1726.PrepareField}).Distinct(StringComparer.Ordinal).OrderBy(x=>x,StringComparer.Ordinal).ToArray();
}
`;
fs.writeFileSync(path.join(host,'FixtureBridge.cs'),bridge);
const r5path=path.join(repo,'tools/inputhost_utf8_guard_perf_selftest.cs'),r5bytes=fs.readFileSync(r5path),r5=r5bytes.toString('utf8');
const corpusStart=r5.indexOf('    private static Dictionary<string,object?> Frame('),corpusEnd=r5.indexOf('    private static List<Scenario> Corpus()');
if(corpusStart<0||corpusEnd<=corpusStart)throw Error('R5 corpus markers changed');
const corpus=r5.slice(corpusStart,corpusEnd);if(!corpus.includes('ly=0f'))throw Error('R5 accepted wire correction missing');
const fixturePath=path.join(repo,'tools/inputhost_property_name_perf_selftest.cs');const ownedFixture=fs.readFileSync(fixturePath).toString('utf8');
if(ownedFixture.split('    // R5_CORPUS_INSERT').length!==2)throw Error('Fixture corpus marker changed');
const nativePath=path.join(repo,'native/main.cpp'),nativeBytes=fs.readFileSync(nativePath),nativeText=nativeBytes.toString('utf8');
const nativeStart=nativeText.indexOf('static std::string inputHostBuildSubmitFrameText(');if(nativeStart<0)throw Error('Native wire source not found');
const nativeMethod=nativeText.slice(nativeStart,closeBrace(nativeText,nativeText.indexOf('{',nativeStart))+1);
const nativeOrder=[...nativeMethod.matchAll(/(?:field|key)\("([^"]+)"\);/g)].map(m=>m[1]);
if(nativeOrder.length!==32||new Set(nativeOrder).size!==32)throw Error('Actual native key order changed');
fs.writeFileSync(path.join(out,'native-wire-method.cpp'),nativeMethod);
fs.writeFileSync(path.join(out,'native-wire-order.json'),JSON.stringify({sourcePath:nativePath,sourceSha256:sha(nativeBytes),methodSha256:sha(nativeMethod),keys:nativeOrder,note:'Native key order only; fixture values/numeric text prepared by C# serializer, no Native entrypoint executed.'},null,2));
const fixture=ownedFixture.replace('    // R5_CORPUS_INSERT',corpus).replace('    // NATIVE_ORDER_INSERT','    private static readonly string[] NativeOrder='+JSON.stringify(nativeOrder).replace(/^\[/,'{').replace(/\]$/,'}')+';');
fs.writeFileSync(path.join(host,'PropertyNameFixture.cs'),fixture);
const projectSource=snapshots.find(f=>f.relative==='YeManInputHost.csproj').bytes.toString('utf8');
let project=projectSource.replace(/<HintPath>(.*?)<\/HintPath>/gs,(_,p)=>'<HintPath>'+path.resolve(liveSource,p.replace('$(MSBuildThisFileDirectory)',liveSource+path.sep)).replaceAll('&','&amp;')+'</HintPath>')
.replace('<AssemblyName>YeManInputHost</AssemblyName>','<AssemblyName>InputHostPropertyNameSelftest</AssemblyName>')
.replace('<PropertyGroup>','<PropertyGroup>\n<StartupObject>PropertyNameFixture</StartupObject>\n<NuGetAudit>false</NuGetAudit>\n<EnableNETAnalyzers>false</EnableNETAnalyzers>\n<TieredCompilation>true</TieredCompilation>\n<TieredPGO>true</TieredPGO>\n<RuntimeFrameworkVersion>10.0.11</RuntimeFrameworkVersion>\n<RollForward>Disable</RollForward>');
fs.writeFileSync(path.join(host,'YeManInputHost.csproj'),project);
const references=[...project.matchAll(/<HintPath>(.*?)<\/HintPath>/gs)].map(x=>x[1].replaceAll('&amp;','&')).map(p=>({path:p,sha256:sha(fs.readFileSync(p)),bytes:fs.statSync(p).size}));
const manifest={productionModified:false,strategy:'adaptive-half-misses-8-message-cooldown',cooldownObjectMessages:8,probeMissThreshold:'misses*2 >= min(propertyCount,64), count>0',
 plainControlBinding:{path:plainRoot,parserSha256:sha(plainParserBytes),helperSha256:sha(plainHelperBytes)},cooldownFallbackLoopIdentical:loop.includes(originalLoop),sourceMode,sourcePath:source,liveProductionPath:liveSource,liveProductionSha256AtStart:sha(liveProgramBytes),liveProductionParserUnchangedAtStart:true,beforeSnapshotPath:baseline,programSha256:sha(programBytes),expectedBeforeProgramSha256:expected,approvedParentAxisCapacityEdits:approvedCapacityEdits,programMatchesBeforeExceptApprovedAxisCapacities:true,
 sourceFiles:snapshots.map(f=>({path:f.relative,sha256:sha(f.bytes),bytes:f.bytes.length})),beforeSnapshotMatchesCurrent:sha(programBytes)===expected,allOtherSourceFilesMatchBefore:true,
 beforeParserSha256:sha(method),candidateParserSha256:sha(candidate),candidateCanonicalSha256:sha(candidateCanonical),candidateHelperSha256:sha(helper),
 parserOutsideEnumerationUnchanged:method.slice(0,start)===candidateCanonical.slice(0,start)&&method.slice(end)===candidateCanonical.slice(start+loop.length),
 candidateScope:'Only extracted enumeration plus ThreadStatic string[64]/int cooldown. At least half misses among first64 after enumeration/duplicate return =>8 subsequent successfully parsed object messages use verbatim original Name loop; then probe. Hints retain canonical known keys only. Original production .cs unchanged.',
 hintOwnership:'Only canonical keys in FrameFields/HidHideFields plus exact protocol metadata/fence constants. No unknown-input strings, JsonElement or JsonDocument in hints. Position >=64 uses property.Name. Every hit verifies NameEquals.',
 baselineFieldCache:'Existing t_fieldCache behavior is retained; its JsonElement retention is not changed by this candidate.',
 r5CorpusSourcePath:r5path,r5CorpusSourceSha256:sha(r5bytes),r5CorpusFragmentSha256:sha(corpus),correctedR5Corpus:true,
 fixtureSha256:sha(fixture),fixtureTemplateSha256:sha(ownedFixture),runnerSha256:sha(fs.readFileSync(fileURLToPath(import.meta.url))),references,
 runtimeRequested:'Microsoft.NETCore.App 10.0.11, RollForward Disable; TieredCompilation/TieredPGO enabled.',
 executionScope:'Fixture StartupObject only. Actual copied Program parser/helpers/hash and static data initialization; no original Host Main, pipe, SDK methods, device/driver calls.',
 staticInitialization:'Retained data-only field initialization including SDK typeof/GetField metadata reflection; RuntimeSharedDependencies initializer registers resolver only.',completed:false};
fs.writeFileSync(path.join(out,'source-manifest.json'),JSON.stringify(manifest,null,2));
if(opts['prepare-only']==='true'){console.log('Prepared only: '+out);process.exit(0);}
function run(args,log,env=process.env,timeout=180000){const p=spawnSync('dotnet',args,{cwd:host,encoding:'utf8',env,timeout,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(out,log),(p.stdout??'')+(p.stderr??''));if(p.error||p.status!==0)throw Error('dotnet '+log+' failed: '+(p.error??p.status));return p.stdout;}
manifest.dotnetInfo=run(['--info'],'dotnet-info.log');
const env={...process.env,DOTNET_TieredCompilation:'1',DOTNET_TieredPGO:'1',DOTNET_ReadyToRun:'1',DOTNET_CLI_TELEMETRY_OPTOUT:'1'};
const empty=path.join(out,'empty-nuget-source');fs.mkdirSync(empty);run(['restore','YeManInputHost.csproj','--source',empty,'-p:NuGetAudit=false'],'restore.log');
const profiles=[];
for(const profile of ['Release','Debug']){
 run(['build','YeManInputHost.csproj','-c',profile,'--no-restore','--nologo'],'build-'+profile+'.log');console.log(profile+': actual-copy Host compiled; fixture entry only');
 const dir=path.join(out,profile.toLowerCase());fs.mkdirSync(dir);
 const dll=path.join(host,'bin',profile,'net10.0-windows','InputHostPropertyNameSelftest.dll');
 run([dll,dir,profile==='Release'?'benchmark':'verify',String(rounds)],'run-'+profile+'.log',env,240000);
 const result=JSON.parse(fs.readFileSync(path.join(dir,'result.json'),'utf8'));profiles.push({profile,...result});
 console.log(profile+': '+result.functionalAssertions+' assertions; '+result.r5ParserCases+' R5 + '+result.r6ParserCases+' R6 parser cases');
}
manifest.completed=true;manifest.liveProductionSha256AtEnd=sha(fs.readFileSync(path.join(liveSource,'Program.cs')));manifest.liveProductionParserUnchangedAtEnd=fs.readFileSync(path.join(liveSource,'Program.cs'),'utf8').includes(method);manifest.sourceStableDuringRun=snapshots.every(f=>sha(fs.readFileSync(path.join(source,f.relative)))===sha(f.bytes));
manifest.referencesStableDuringRun=references.every(f=>sha(fs.readFileSync(f.path))===f.sha256);
manifest.r5ToolStableDuringRun=sha(fs.readFileSync(r5path))===manifest.r5CorpusSourceSha256;
manifest.copiedProductionCsUnchanged=snapshots.filter(f=>f.relative.endsWith('.cs')).every(f=>sha(fs.readFileSync(path.join(host,f.relative)))===sha(f.bytes));
fs.writeFileSync(path.join(out,'source-manifest.json'),JSON.stringify(manifest,null,2));
const result={ok:true,productionModified:false,strategy:manifest.strategy,runDirectory:out,manifest,profiles};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));fs.writeFileSync(path.join(root,'result.json'),JSON.stringify(result,null,2));
if(!manifest.sourceStableDuringRun||!manifest.referencesStableDuringRun||!manifest.r5ToolStableDuringRun||!manifest.copiedProductionCsUnchanged)throw Error('Source/dependency binding changed; retain snapshot and stop');
console.log('Evidence: '+out);
