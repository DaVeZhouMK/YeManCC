"""Extract actual FanHost template selection/caller slices; run only inert C# seams."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess, sys
parser=argparse.ArgumentParser()
parser.add_argument('output',type=Path)
parser.add_argument('--source',type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
source=args.source or root/'FanLab/real-host/Program.cs'
raw=source.read_bytes();text=raw.decode('utf-8-sig')
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
def method(name):
    matches=list(re.finditer(r'    private (?:void|object\?) '+name+r'\([^\n]*\)\s*\{',text))
    if len(matches)!=1:raise RuntimeError('ambiguous method '+name)
    start=matches[0].start();brace=text.index('{',matches[0].start());depth=1;end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
capture=method('CaptureHcProfileTemplate')
select=method('SelectHcDefaultProfileForPowerLine')
marker='        // FAN-929（2026-09-27 用户裁决）：把"AC/DC 电源档"对齐'
if text.count(marker)!=1:raise RuntimeError('ambiguous Enable refresh marker')
a=text.index(marker)
b=text.index('        var profile = BuildPowerProfile(duties, software: true);',a)
enable=text[a:b]
watch=re.findall(r'                if \(profilePowerLineOnline.Value != online\)\s*CaptureHcProfileTemplate\((?:online)?\);',text)
if len(watch)!=1:raise RuntimeError('ambiguous watchdog capture')
extracted={'capture':capture,'select':select,'enableRefreshSlice':enable,'watchdogCaptureSlice':watch[0]}
(out/'extracted.json').write_text(json.dumps(extracted,ensure_ascii=False,indent=2),encoding='utf-8')
scaffold=r"""
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text.Json;
record Profile(string Guid,int? OEMPowerMode=null,bool DeviceDefault=false);
sealed class Device {public object[] DevicePowerProfiles {get;}=new object[]{new Profile("battery"),new Profile("ac"),new Profile("lenovo",7),new Profile("msi"),new Profile("fallback",DeviceDefault:true)};}
static class Ids {public const string BetterPerformanceGuid="battery",BestPerformanceGuid="ac";}
sealed class Sampler {
    private object? device=new Device();
    private int? baselineSmartFanMode,baselineMsiShiftValue;
    private object? hcProfileTemplate;
    private bool? profilePowerLineOnline;
    private string? profileSelectionSource,profileTemplateFingerprint;
    private Queue<bool> samples;
    private bool last,missingType;
    public int Reads {get;private set;}
    public int Clones {get;private set;}
    public bool ThrowClone;
    public bool? Online=>profilePowerLineOnline;
    public string? Guid=>(hcProfileTemplate as Profile)?.Guid;
    public Sampler(string route,bool first,bool second,bool? previous=null){samples=new(new[]{first,second,second,second,second});last=second;profilePowerLineOnline=previous;hcProfileTemplate=new Profile("seed");baselineSmartFanMode=route=="lenovo"?7:null;baselineMsiShiftValue=route=="msi"?1:null;missingType=route=="fallback";}
    private bool IsPowerLineOnline(){Reads++;if(samples.Count>0)last=samples.Dequeue();return last;}
    private static object? GetMember(object target,string name)=>target.GetType().GetProperty(name)?.GetValue(target);
    private Type? FindType(string name)=>missingType?null:typeof(Ids);
    private static object? GetStaticMember(Type type,string name)=>type.GetField(name)?.GetValue(null);
    private object? SelectMsiProfileByCurrentShift(IReadOnlyList<object> candidates)=>baselineMsiShiftValue is null?null:candidates.First(x=>Equals(GetMember(x,"Guid"),"msi"));
    private object CloneHcPowerProfilePreservingFanState(object source){Clones++;if(ThrowClone)throw new InvalidOperationException("CLONE_FAILED");return ((Profile)source) with{};}
    private static string DescribeProfileFingerprint(object? source)=>(source as Profile)?.Guid??"none";
    public void Capture()=>CaptureHcProfileTemplate();
    public void MissingDevice(){device=null;CaptureHcProfileTemplate();}
    public void EmptyProfiles(){device=new {DevicePowerProfiles=Array.Empty<object>()};CaptureHcProfileTemplate();}
    public void EnableRefresh(){
@@ENABLE@@
    }
    public void WatchdogRefresh(){
        var online=IsPowerLineOnline();
@@WATCH@@
        profilePowerLineOnline=online;
    }
@@CAPTURE@@
@@SELECT@@
}
record Result(string Name,bool Ok,string? Error,int PowerReads,int Clones,string? Guid,bool? Online);
static class Program {
 static readonly List<Result> Rows=new();
 static void Expect(bool value,string reason){if(!value)throw new Exception(reason);}
 static string Expected(string route,bool online)=>route switch{"lenovo"=>"lenovo","msi"=>"msi","fallback"=>"fallback",_=>online?"ac":"battery"};
 static void Check(string name,Sampler f,Action body){try{body();Rows.Add(new(name,true,null,f.Reads,f.Clones,f.Guid,f.Online));}catch(Exception e){Rows.Add(new(name,false,e.Message,f.Reads,f.Clones,f.Guid,f.Online));}}
 static int Main(){
  foreach(var route in new[]{"default","lenovo","msi","fallback"})foreach(var first in new[]{false,true})foreach(var second in new[]{false,true}){
   foreach(var stage in new[]{"capture","enable-changed","enable-unknown","watchdog"}){
    var f=new Sampler(route,first,second,stage is "enable-changed" or "watchdog"?!first:null);
    Check($"{route}/{stage}/{first}->{second}",f,()=>{
     switch(stage){case "capture":f.Capture();break;case "watchdog":f.WatchdogRefresh();break;default:f.EnableRefresh();break;}
     Expect(f.Guid==Expected(route,first),$"wrong selected template: {f.Guid}; expected {Expected(route,first)}");
     Expect(f.Online==first,$"selected template and recorded power disagree: {f.Guid}/{f.Online}; observed {first}");
     Expect(f.Reads==1,$"same operation sampled power {f.Reads} times, expected 1");
     Expect(f.Clones==1,"normal capture must still detach one HC template");
    });
   }
  }
  foreach(var route in new[]{"default","lenovo","msi","fallback"})foreach(var first in new[]{false,true}){
   var f=new Sampler(route,first,!first,first);Check($"{route}/unchanged/{first}",f,()=>{f.EnableRefresh();Expect(f.Reads==1,"unchanged path did not use one live observation");Expect(f.Clones==0&&f.Guid=="seed"&&f.Online==first,"unchanged current template was replaced");});
  }
  foreach(var route in new[]{"default","lenovo","msi","fallback"})foreach(var first in new[]{false,true}){
   var f=new Sampler(route,first,!first);Check($"{route}/successive-captures/{first}",f,()=>{f.Capture();f.Capture();Expect(f.Online==!first&&f.Guid==Expected(route,!first),"successful old observation leaked into the next operation");Expect(f.Reads==2&&f.Clones==2,"separate captures must each read the current power state once");});
  }
  foreach(var first in new[]{false,true}){
   var f=new Sampler("default",first,!first);Check($"failed-clone-retry/{first}",f,()=>{f.ThrowClone=true;try{f.Capture();throw new Exception("clone error swallowed");}catch(InvalidOperationException e)when(e.Message=="CLONE_FAILED"){}Expect(f.Online is null&&f.Guid=="seed","failed clone published a new template or power marker");f.ThrowClone=false;f.Capture();Expect(f.Online==!first&&f.Guid==Expected("default",!first),"failed sample was cached into the next attempt");Expect(f.Reads==2,"retry did not take a new live power observation");});
  }
  var missing=new Sampler("default",false,true);Check("missing-device-before-power-read",missing,()=>{try{missing.MissingDevice();throw new Exception("missing device accepted");}catch(InvalidOperationException){}Expect(missing.Reads==0,"missing device caused an unnecessary power query");});
  var empty=new Sampler("default",false,true);Check("missing-profile-before-power-read",empty,()=>{try{empty.EmptyProfiles();throw new Exception("missing profile accepted");}catch(InvalidOperationException){}Expect(empty.Reads==0,"missing profile caused an unnecessary power query");});
  var ok=Rows.All(x=>x.Ok);Console.WriteLine(JsonSerializer.Serialize(new{ok,checks=Rows.Count,results=Rows,hardwareCalls=0,scope="actual extracted capture/default-selection and actual Enable/watchdog caller slices; fake profiles, cloning and power source; no real Host engine or device proof"},new JsonSerializerOptions{PropertyNamingPolicy=JsonNamingPolicy.CamelCase}));return ok?0:1;
 }
}
"""
program=scaffold.replace('@@CAPTURE@@',capture).replace('@@SELECT@@',select).replace('@@ENABLE@@',enable).replace('@@WATCH@@',watch[0])
project=out/'harness';project.mkdir(exist_ok=True)
(project/'Program.cs').write_text(program,encoding='utf-8')
(project/'Gate.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><AssemblyName>FanPowerLineSnapshotGate</AssemblyName></PropertyGroup></Project>',encoding='utf-8')
build=subprocess.run(['dotnet','build',str(project/'Gate.csproj'),'-c','Release','-o',str(project/'bin'),'--nologo','--ignore-failed-sources','-p:UseSharedCompilation=false','-p:NuGetAudit=false'],capture_output=True,encoding='utf-8',errors='replace',timeout=180)
(out/'build.txt').write_text(build.stdout+build.stderr,encoding='utf-8')
if build.returncode:print(build.stdout+build.stderr);raise SystemExit(build.returncode)
run=subprocess.run(['dotnet',str(project/'bin/FanPowerLineSnapshotGate.dll')],capture_output=True,encoding='utf-8',errors='replace',timeout=30)
(out/'output.txt').write_text(run.stdout+run.stderr,encoding='utf-8')
lines=[line for line in run.stdout.splitlines() if line.startswith('{')]
if len(lines)!=1:raise RuntimeError('missing gate JSON '+run.stderr)
data=json.loads(lines[0]);data['sourceSha256']=hashlib.sha256(raw).hexdigest();data['testSha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest();data['extractedSha256']=hashlib.sha256(program.encode('utf-8')).hexdigest();data['buildExit']=build.returncode;data['runExit']=run.returncode
(out/'result.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
bad=[x for x in data['results'] if not x['ok']]
print(json.dumps({'ok':data['ok'],'checks':data['checks'],'failures':len(bad),'firstFailures':bad[:3]},ensure_ascii=False))
raise SystemExit(run.returncode)
