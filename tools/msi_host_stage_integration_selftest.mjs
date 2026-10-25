import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const root=path.resolve(process.argv[2] || 'G:/YeManCC-Work/Build/Validation/MSI-Host-Stage-R8-20261013/integration');
fs.mkdirSync(root,{recursive:true});
const opts={encoding:'utf8',timeout:60000,windowsHide:true,env:{...process.env,DOTNET_CLI_HOME:root+'/cli-home',NUGET_PACKAGES:root+'/packages',DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_SKIP_FIRST_TIME_EXPERIENCE:'1'}};
const source=fs.readFileSync(repo+'/InputHost/Program.cs','utf8').replaceAll('\r\n','\n');
const before=fs.readFileSync(process.argv[3] || root+'/Program-before.cs','utf8').replaceAll('\r\n','\n');
const hash=s=>createHash('sha256').update(s).digest('hex');
function extract(text,name){
 const re=new RegExp('^\\s*(?:private|public|internal) (?:static )?(?:async )?[^\\n;]+\\b'+name+'\\([^;]*?\\)\\s*\\{','m');
 const m=re.exec(text);assert.ok(m,'method '+name);const start=m.index+m[0].lastIndexOf('{');let depth=1,state='code';
 for(let i=start+1;i<text.length;i++){const c=text[i],n=text[i+1];if(state==='line'){if(c==='\n')state='code';continue}if(state==='block'){if(c==='*'&&n==='/'){state='code';i++}continue}if(state==='str'||state==='char'){if(c==='\\'){i++;continue}if(c===(state==='str'?'"':"'"))state='code';continue}if(c==='/'&&n==='/'){state='line';i++;continue}if(c==='/'&&n==='*'){state='block';i++;continue}if(c==='"'){state='str';continue}if(c==="'"){state='char';continue}if(c==='{')depth++;if(c==='}'&&--depth===0)return text.slice(m.index,i+1).trim();}throw Error('unbalanced '+name);
}
function stripExtension(s){
 return s.replace(/(^[ \t]*)_msiHostStageEvidence\?\.Begin(Backend|Cost)\(\);\n\1try\n\1\{\n([\s\S]*?)\n\1\}\n\1finally\n\1\{\n\1    _msiHostStageEvidence\?\.End\2\(\);\n\1\}/gm,
 (_all,pad,_kind,body)=>body.split('\n').map(line=>line.startsWith(pad+'    ')?line.slice(4):line).join('\n'));
}
const parsed=extract(source,'TryParseArguments');
const removeHooks=s=>s.replace(/^\s*_msiHostStageEvidence\?\.(?:BeginRead|EndRead|BeginHandle|EndHandle|BeginWrite|EndWrite|BeginSubmit|EndSubmit)\([^\n]*\);\n/gm,'')
 .replace(/\n\s*\/\/ Parsed metadata is diagnostic evidence, never tuple\/admission proof\.\n\s*var stageEnvelope = envelope!;\n\s*_msiHostStageEvidence\?\.Decode\(stageEnvelope.RequestId, stageEnvelope.Sequence,\n\s*stageEnvelope.RunId, stageEnvelope.Epoch, stageEnvelope.PowerGeneration, stageEnvelope.Command\);/,'')
 .replace(/\n\s*finally\n\s*\{\s*\}/,'')
 .replace(/\n\s*if \(_msiHostStageEvidence is \{ \} stage\)\n\s*\{[\s\S]*?\n\s*catch \{ \}\n\s*\}/,'');
const norm=s=>s.split('\n').filter(x=>x.trim()).join('\n');
const rows=['RunPipeClientAsync','HandleLocked','TrySubmitFrame','MaybeEmitCostWindow','LogFatalExit'].map(name=>{const a=removeHooks(extract(before,name)),b=removeHooks(stripExtension(extract(source,name)));return {name,original:hash(norm(a)),withoutDiagnostic:hash(norm(b)),same:norm(a)===norm(b)}});
assert.ok(rows.every(x=>x.same),JSON.stringify(rows));
const projectDir=root+'/parser-only';fs.mkdirSync(projectDir,{recursive:true});
const fixture=`using System;using System.Linq;using System.Text.Json;internal static class ParserFixture{${parsed}\nstatic int Main(){int assertions=0,cases=0;void Test(string[] a,bool accepted,bool stage){cases++;var ok=TryParseArguments(a,out var pipe,out var nonce,out var pid,out var requested,out var error);assertions++;if(ok!=accepted||(ok&&requested!=stage))throw new Exception("case "+cases);if(ok){assertions++;if(pipe!="pipe-name"||nonce!=new string('a',32)||pid!=111)throw new Exception("values changed");}}string[] Good()=>new[]{"--pipe","pipe-name","--nonce",new string('a',32),"--parent-pid","111"};Test(Good(),true,false);Test(Good().Append("--msi-host-stage-evidence-v1").ToArray(),true,true);Test(Good().Append("--other").ToArray(),false,false);Test(Good().Append("--MSI-HOST-STAGE-EVIDENCE-V1").ToArray(),false,false);Test(Good().Concat(new[]{"--msi-host-stage-evidence-v1","extra"}).ToArray(),false,false);for(int n=0;n<6;n++)Test(Good().Take(n).ToArray(),false,false);foreach(var i in new[]{0,2,4}){var a=Good();a[i]="--wrong";Test(a,false,false);}foreach(var v in new[]{"", " ",new string('p',181),"pipe\\n"}){var a=Good();a[1]=v;Test(a,false,false);}foreach(var v in new[]{"",new string('a',31),new string('a',161),new string('a',32)+"\\n"}){var a=Good();a[3]=v;Test(a,false,false);}foreach(var v in new[]{"0","-1","abc","2147483648"}){var a=Good();a[5]=v;Test(a,false,false);}Console.WriteLine(JsonSerializer.Serialize(new{passed=true,cases,assertions,productionAssemblyLoaded=false,realMappingCalls=0,scope="Actual TryParseArguments extracted alone; old six arguments and exact opt-in seventh flag"}));return 0;}}`;
fs.writeFileSync(projectDir+'/ParserFixture.cs',fixture);
fs.writeFileSync(projectDir+'/ParserFixture.csproj','<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><NuGetAudit>false</NuGetAudit><PlatformTarget>x64</PlatformTarget></PropertyGroup></Project>');
fs.writeFileSync(projectDir+'/NuGet.Config','<configuration><packageSources><clear /></packageSources></configuration>');
const built=spawnSync('dotnet',['build',projectDir+'/ParserFixture.csproj','-c','Release'],opts);fs.writeFileSync(projectDir+'/build.log',(built.stdout||'')+(built.stderr||''));assert.equal(built.status,0,'parser fixture compile');
const run=spawnSync('dotnet',[projectDir+'/bin/Release/net10.0/ParserFixture.dll'],opts);fs.writeFileSync(projectDir+'/result.log',(run.stdout||'')+(run.stderr||''));assert.equal(run.status,0,'parser fixture checks');
const result=JSON.parse(run.stdout.trim());
assert.equal(fs.readFileSync(repo+'/InputHost/Program.cs','utf8').replaceAll('\r\n','\n'),source,'Program stable during isolated parser test');
fs.writeFileSync(root+'/integration-semantic-binding.json',JSON.stringify({sourceSha256:hash(source),beforeSha256:hash(before),uninstrumentedHandlerBodiesEqual:true,handlers:rows,parser:result,scope:'Source equivalence after removing only diagnostic calls/blocks, plus actual extracted argument parser. Not full runtime handler execution or Steam acceptance.'},null,2));
console.log(JSON.stringify({handlersPreserved:rows.length,parser:result}));

// Execute extracted production hooks with synthetic backend/diagnostic bodies only.
// The actual synchronous cost payload is compared above, then replaced as one opaque
// seam; no process enumeration, filesystem logging, SDK, device or mapping is invoked.
function hookMethods(text){
 let cost=extract(text,'MaybeEmitCostWindow');
 const begin=cost.indexOf('var count = _costSubmitUsCount;');
 const last='line + Environment.NewLine, System.Text.Encoding.UTF8);';
 const end=cost.indexOf(last,begin)+last.length;
 assert.ok(begin>0&&end>begin,'exact cost payload seam');
 cost=cost.slice(0,begin)+'CostDiagnostics();'+cost.slice(end);
 const reset=extract(text,'ResetCostWindow').replace('ResetCostWindow()','ResetCostWindowActual()');
 return [extract(text,'TrySubmitFrame'),extract(text,'RecordCostFrame'),cost,reset].join('\n');
}
const oldMethods=hookMethods(before),newMethods=hookMethods(source);
const hooksDir=root+'/actual-hooks';fs.mkdirSync(hooksDir,{recursive:true});
const hooks=String.raw`
using System;using System.IO;using System.Linq;using System.Collections.Generic;using System.Buffers.Binary;using System.Text.Json;
using YeManInputHost.Diagnostics;using Environment=SyntheticEnvironment;using Stopwatch=SyntheticStopwatch;
internal static class SyntheticEnvironment{internal static long TickCount64=>5000;}
internal static class SyntheticStopwatch{internal static long GetTimestamp()=>2000;internal static TimeSpan GetElapsedTime(long _)=>TimeSpan.FromTicks(1000);}
internal record Config(string Persona="steamdeck",bool Gate=true,bool Due=true,bool Cost=true,bool Flag=true,bool Ready=true,bool ProfileThrow=false,bool BuildThrow=false,bool BackendThrow=false,bool DiagnosticThrow=false,bool FlagThrow=false,bool ResetThrow=false,int SinkFailurePhase=0);
internal record CommandEnvelope(string Persona,ulong Sequence=3376,ulong BackButtons=0);
internal abstract class HarnessBase{
 protected MsiHostStageEvidence? _msiHostStageEvidence;
 protected FakeController? _controller;protected FakeContext? _context;protected Config cfg=new();
 protected bool _costFlagEnabled;protected long _costWindowStartTicks;
 protected long _costFrames,_costFramesRejected,_costJsonParseUsSum,_costJsonParseCount,_costHidSubmitUsSum,_costHidSubmitCount,_costPipeReadUsSum,_costPipeReadCount,_costHidHideQueryFull,_costHidHideQueryState;
 protected int _costSubmitUsCount;protected long[] _costSubmitUsSamples=new long[32];
 protected long _lastSubmitElapsedUs,_backendSubmitOk,_backendSubmitFail;protected string _lastBackendDetail="";protected ulong _lastBackendSequence;
 protected uint _dualSenseFrameCounter,_ds4FrameCounter;
 internal readonly List<string> Business=new(),Order=new();internal readonly List<int> Stages=new();internal readonly List<long> FramesAtCostReturn=new();
 internal readonly Clock clock=new();internal Sink? sink;internal int FactoryCalls;
 internal void Init(Config c){cfg=c;_costFlagEnabled=c.Cost;_costWindowStartTicks=c.Due?0:4900;_controller=c.Ready?new FakeController(this):null;_context=c.Ready?new FakeContext(this):null;
  if(c.Gate){sink=new Sink(this);_msiHostStageEvidence=MsiHostStageEvidence.TryCreateForTest(111,222,"0123456789abcdef0123456789abcdef",clock,new Factory(this));
   if(_msiHostStageEvidence is null)throw new Exception("fixture create");_msiHostStageEvidence.BeginRead();_msiHostStageEvidence.EndRead(123);_msiHostStageEvidence.BeginHandle();_msiHostStageEvidence.Decode("req-3376",3376,"run-7",7,3,"SUBMIT_FRAME");}}
 internal void Mark(string s){Business.Add(s);Order.Add(s);}
 internal void Stage(int phase){Stages.Add(phase);Order.Add("s"+phase);if(phase==15)FramesAtCostReturn.Add(_costFrames);}
 internal abstract bool Run(CommandEnvelope e,out string detail);
 protected object Build(string kind){Mark("build:"+kind);if(cfg.BuildThrow)throw new FormatException("synthetic build");return new object();}
 protected object BuildEliteState(object p,CommandEnvelope e)=>Build("xbox");
 protected object BuildDualSenseDataOnlyReport(object p,CommandEnvelope e,uint f,object? b=null)=>Build("sony");
 protected object MapDsEdgePaddles(ulong b){Mark("paddles");return new object();}
 protected object BuildDs4V2DataOnlyReport(object p,CommandEnvelope e,uint f)=>Build("ds4");
 protected void SubmitSteamDeckWithScreenPads(FakeController c,object p,CommandEnvelope e){Build("sd");c.SubmitState(new object());}
 protected void SubmitSonyReportChain1410(FakeController c,CommandEnvelope e,object r,uint f,string why)=>c.SubmitRawReport(r);
 protected void SubmitEliteReportChain1410(FakeController c,object s,CommandEnvelope e,string why)=>c.SubmitState(s);
 protected void RecordCostRole(string role)=>Mark("role:"+role);
 protected bool CostLoggingRequestedByFlag(){Mark("flag");if(cfg.FlagThrow)throw new IOException("synthetic flag");return cfg.Flag;}
 protected void CostDiagnostics(){Mark("diagnostics");if(cfg.DiagnosticThrow)throw new InvalidOperationException("synthetic diagnostic");}
 protected void BeforeReset(){Mark("reset");}protected void AfterReset(){if(cfg.ResetThrow)throw new ArithmeticException("synthetic reset");}
 internal string Outcome(){string detail="",failure="";bool? ok=null;try{ok=Run(new(cfg.Persona),out detail);}catch(Exception ex){failure=ex.GetType().Name;}
  return JsonSerializer.Serialize(new{ok,detail,failure,Business,_backendSubmitOk,_backendSubmitFail,_lastBackendDetail,_lastBackendSequence,_lastSubmitElapsedUs,_costFrames,_costFramesRejected,_costSubmitUsCount,_costHidSubmitCount,_costHidSubmitUsSum,_costFlagEnabled,_costWindowStartTicks,_dualSenseFrameCounter,_ds4FrameCounter});}
 internal sealed class FakeContext{readonly HarnessBase h;internal FakeContext(HarnessBase h){this.h=h;}internal object GetProfile(string name){h.Mark("profile:"+name);if(h.cfg.ProfileThrow)throw new InvalidOperationException("synthetic profile");return new object();}}
 internal sealed class FakeController{readonly HarnessBase h;internal FakeController(HarnessBase h){this.h=h;}void Submit(string kind){h.Mark("sdk:"+kind);if(h.cfg.BackendThrow)throw new InvalidOperationException("synthetic backend");}internal void SubmitState(object o)=>Submit("state");internal void SubmitRawReport(object o)=>Submit("raw");}
 internal sealed class Clock:MsiHostStageEvidence.IClock{internal int Calls;public long Frequency=>1000;public long GetTimestamp()=>1000+Calls++;}
 internal sealed class Factory:MsiHostStageEvidence.ISinkFactory{readonly HarnessBase h;internal Factory(HarnessBase h){this.h=h;}public MsiHostStageEvidence.ISink CreateNew(string name,int capacity){h.FactoryCalls++;if(capacity!=1024)throw new Exception("capacity");return h.sink!;}}
 internal sealed class Sink:MsiHostStageEvidence.ISink{readonly HarnessBase h;internal byte[] Bytes=new byte[1024];internal int Operations;bool failed;
  internal Sink(HarnessBase h){this.h=h;}public void WriteSequence(ulong s){Operations++;BinaryPrimitives.WriteUInt64LittleEndian(Bytes.AsSpan(24,8),s);if((s&1)==0){int phase=(int)BinaryPrimitives.ReadUInt64LittleEndian(Bytes.AsSpan(56,8));h.Stage(phase);if(!failed&&phase==h.cfg.SinkFailurePhase){failed=true;throw new IOException("synthetic sink");}}}
  public void WriteData(byte[] b,int offset,int count){Operations++;b.AsSpan(offset,count).CopyTo(Bytes.AsSpan(offset,count));}public void Dispose(){} }
}
internal sealed class BeforeHarness:HarnessBase{
 internal override bool Run(CommandEnvelope e,out string detail)=>TrySubmitFrame(e,out detail);
 private void ResetCostWindow(){BeforeReset();ResetCostWindowActual();AfterReset();}
${oldMethods}
}
internal sealed class AfterHarness:HarnessBase{
 internal override bool Run(CommandEnvelope e,out string detail)=>TrySubmitFrame(e,out detail);
 private void ResetCostWindow(){BeforeReset();ResetCostWindowActual();AfterReset();}
${newMethods}
}
internal static class HookFixture{
 static int checks,cases;static void Check(bool b,string message){checks++;if(!b)throw new Exception(message);}
 static int Main(){try{
 var variations=new[]{new Config(),new Config(Due:false),new Config(Cost:false),new Config(Flag:false),new Config(Ready:false),new Config(ProfileThrow:true),new Config(BuildThrow:true),new Config(BackendThrow:true),new Config(DiagnosticThrow:true),new Config(FlagThrow:true),new Config(ResetThrow:true),new Config(SinkFailurePhase:12),new Config(SinkFailurePhase:14)};
 foreach(var persona in new[]{"steamdeck","xbox360","dualsense","elite","dualsense-edge","other"})foreach(var v in variations)foreach(var gate in new[]{false,true}){
  cases++;var c=v with{Persona=persona,Gate=gate};var old=new BeforeHarness();var now=new AfterHarness();old.Init(c);now.Init(c);
  var original=old.Outcome();var changed=now.Outcome();Check(original==changed,"business behavior changed: "+c+"\n"+original+"\n"+changed);
  if(!gate){Check(now.FactoryCalls==0&&now.clock.Calls==0&&now.sink is null&&now.Stages.Count==0,"closed gate zero observation");continue;}
  Check(now.FactoryCalls==1,"one synthetic writer");
  if(!c.Ready){Check(!now.Stages.Contains(6)&&!now.Stages.Contains(12)&&!now.Stages.Contains(14),"early no target has no submit/backend/cost");continue;}
  if(c.SinkFailurePhase>0){Check(!changed.Contains("synthetic sink"),"sink error fail-open");continue;}
  Check(now.Stages.Count(x=>x==12)==(c.ProfileThrow?0:1),"backend begin only at actual switch");
  Check(now.Stages.Count(x=>x==13)==(c.ProfileThrow?0:1),"backend finally even when switch throws");
  Check(now.Stages.Last()==7,"outer submit finally preserved");
  bool due=c.Due&&c.Cost;Check(now.Stages.Count(x=>x==14)==(due?1:0)&&now.Stages.Count(x=>x==15)==(due?1:0),"cost hooks only after due and finally on throw");
  if(due){Check(now.Order.IndexOf("s14")<now.Order.IndexOf("flag"),"cost begins before flag refresh");
   if(!c.FlagThrow){Check(now.Order.IndexOf("reset")<now.Order.IndexOf("s15")&&now.FramesAtCostReturn.Single()==0,"cost finally follows actual ResetCostWindow");}
   if(!c.ProfileThrow)Check(now.Order.IndexOf("s13")<now.Order.IndexOf("s14"),"backend returned before cost work");
   Check(now.Order.IndexOf("s15")<now.Order.LastIndexOf("s7"),"cost return does not mean submit or ACK returned");}
  if(!c.ProfileThrow){int build=now.Order.FindIndex(x=>x.StartsWith("build:"));Check(now.Order.IndexOf("s12")<build&&build<now.Order.IndexOf("s13"),"actual report build inside switch brackets");}
 }
 Console.WriteLine(JsonSerializer.Serialize(new{passed=true,cases,assertions=checks,realMappingCalls=0,hardwareOrInputApiCalls=0,productionAssemblyLoaded=false,scope="Actual before/after TrySubmitFrame, RecordCostFrame, due-window hooks and ResetCostWindow extracted; synthetic backend, cost payload and clocks; business traces/results equal for all personas/gates/throws"}));return 0;
 }catch(Exception ex){Console.WriteLine(JsonSerializer.Serialize(new{passed=false,cases,assertions=checks,error=ex.ToString()}));return 1;}}
}
`;
fs.writeFileSync(hooksDir+'/HooksFixture.cs',hooks);
fs.copyFileSync(repo+'/InputHost/MsiHostStageEvidence.cs',hooksDir+'/MsiHostStageEvidence.cs');
fs.writeFileSync(hooksDir+'/HooksFixture.csproj','<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>disable</ImplicitUsings><Nullable>enable</Nullable><PlatformTarget>x64</PlatformTarget><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>');
fs.writeFileSync(hooksDir+'/NuGet.Config','<configuration><packageSources><clear /></packageSources></configuration>');
const hooksBuild=spawnSync('dotnet',['build',hooksDir+'/HooksFixture.csproj','-c','Release'],opts);
fs.writeFileSync(hooksDir+'/build.log',(hooksBuild.stdout||'')+(hooksBuild.stderr||''));assert.equal(hooksBuild.status,0,'actual hooks fixture compile; inspect '+hooksDir+'/build.log');
const hooksRun=spawnSync('dotnet',[hooksDir+'/bin/Release/net10.0/HooksFixture.dll'],opts);
fs.writeFileSync(hooksDir+'/result.json',(hooksRun.stdout||'')+(hooksRun.stderr||''));assert.equal(hooksRun.status,0,'actual hooks checks; inspect '+hooksDir+'/result.json');
const hooksResult=JSON.parse(hooksRun.stdout.trim());
fs.writeFileSync(root+'/extension-integration-summary.json',JSON.stringify({passed:true,sourceSha256:hash(source),beforeSha256:hash(before),handlers:rows,parser:result,hooks:hooksResult,diagnosticPayloadActuallyExecuted:false,scope:'Extracted synthetic functions and source-equivalence only; no Host or SDK assembly'},null,2));
console.log(JSON.stringify({actualHooks:hooksResult}));