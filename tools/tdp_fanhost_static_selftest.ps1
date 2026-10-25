param([string]$HostDll,[string]$OutputDir)
$ErrorActionPreference='Stop'
$OutputDir=[IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
# Reflection fixture only: no Host Main, lifetime mutex, HC/WMI, device creation.
$project=@"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.19041.0</TargetFramework><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup></Project>
"@
$code=@"
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Security.Cryptography;
using System.Text.Json;
class FakeDevice { public int Calls; public bool Result=true; public bool Open(){Calls++;return Result;} }
class Entry {
 static int Main(string[] args){
  var file=Path.GetFullPath(args[0]);AssemblyLoadContext.Default.Resolving+=(c,n)=>{var p=Path.Combine(Path.GetDirectoryName(file)!,n.Name+".dll");return File.Exists(p)?c.LoadFromAssemblyPath(p):null;};
  var assembly=AssemblyLoadContext.Default.LoadFromAssemblyPath(file);var type=assembly.GetType("YeManFanHost.RealHcBackend",true)!;var optionsType=assembly.GetType("YeManFanHost.HostOptions",true)!;
  const BindingFlags all=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static;
  var options=optionsType.GetMethod("Parse",all)!.Invoke(null,new object[]{Array.Empty<string>()});var backend=Activator.CreateInstance(type,all,null,new[]{options},null)!;
  var checks=new List<object>();void Check(string name,bool pass)=>checks.Add(new{name,pass});
  var rows=new List<(string,JsonElement)>();Action<string,object?> sink=(name,value)=>rows.Add((name,JsonSerializer.SerializeToElement(value)));
  type.GetProperty("Diagnostic",all)!.SetValue(backend,sink);
  type.GetProperty("PowerObservationContext",all)!.SetValue(backend,(Func<object?>)(()=>new{powerGeneration=41,powerState="Ready",controlCycleId="fixture-cycle"}));
  var begin=type.GetMethod("BeginPowerObservation",all)!;
  using(var scope=(IDisposable?)begin.Invoke(backend,new object?[]{"fixture-owner","fixture-boundary",null})){}
  Check("actual-candidate-Host-static-scope-entered-and-ended",rows.Count(r=>r.Item1=="oem.power-boundary")==2);
  Check("locked-HC-leaf-gap-explicit-not-fabricated",rows.Any(r=>r.Item1=="oem.power-hook-unavailable"&&r.Item2.GetProperty("leafEvidence").GetString()!.Contains("not-observed")));
  Check("scope-ended-not-physical-or-leaf-success",rows.Any(r=>r.Item1=="oem.power-boundary"&&r.Item2.GetProperty("state").GetString()=="scope-ended"&&r.Item2.GetProperty("outcome").GetString()!.StartsWith("not-observed")));
  var before=rows.Count;var device=new FakeDevice();type.GetField("device",all)!.SetValue(backend,device);type.GetMethod("OpenHcDevice",all)!.Invoke(backend,null);
  Check("actual-Open-body-calls-fake-device-exactly-once",device.Calls==1&&(bool)type.GetProperty("IsOpen",all)!.GetValue(backend)!);
  Check("Open-call-correlated-with-Host-boundary",rows.Skip(before).Count(r=>r.Item1=="oem.power-boundary")==2);
  Check("missing-hook-gap-once-per-Host-backend-not-every-call",rows.Count(r=>r.Item1=="oem.power-hook-unavailable")==1);
  before=rows.Count;using(var scope=(IDisposable?)begin.Invoke(backend,new object?[]{"FanHost.HC.Open","OpenHcDevice",null})){}
  Check("repeated-unchanged-Host-boundary-detail-not-permanent-base",rows.Skip(before).Count(r=>r.Item1=="oem.power-boundary-sample")==2&&!rows.Skip(before).Any(r=>r.Item1=="oem.power-boundary"));
  type.GetProperty("Diagnostic",all)!.SetValue(backend,(Action<string,object?>)((_,_)=>throw new IOException("fixture-sink-failure")));
  type.GetMethod("OpenHcDevice",all)!.Invoke(backend,null);Check("logging-failure-does-not-change-Open-return-or-call-count",device.Calls==2);
  device.Result=false;bool failed=false;try{type.GetMethod("OpenHcDevice",all)!.Invoke(backend,null);}catch(TargetInvocationException e){failed=e.InnerException is InvalidOperationException;}
  Check("logging-failure-does-not-swallow-original-Open-failure",failed&&device.Calls==3);
  var hcLoaded=AppDomain.CurrentDomain.GetAssemblies().Any(a=>a.GetName().Name=="HandheldCompanion");Check("no-HC-runtime-loaded-no-device-construction-no-hardware",!hcLoaded);
  var elements=checks.Select(c=>JsonSerializer.SerializeToElement(c)).ToArray();var failures=elements.Count(c=>!c.GetProperty("pass").GetBoolean());
  var report=new{checks,failures,hostPath=file,hostSha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(file))),actualLoadedHostMvid=assembly.ManifestModule.ModuleVersionId.ToString("D"),HCLoaded=hcLoaded,realHardwareExecuted=false,scope="ACTUAL_HOST_ASSEMBLY_REFLECTION_STATIC_BOUNDARY_AND_FAKE_OPEN_ONLY"};
  File.WriteAllText(args[1],JsonSerializer.Serialize(report,new JsonSerializerOptions{WriteIndented=true}));Console.WriteLine(JsonSerializer.Serialize(report,new JsonSerializerOptions{WriteIndented=true}));return failures==0?0:1;
 }
}
"@
Set-Content -LiteralPath (Join-Path $OutputDir 'Fixture.csproj') -Value $project -Encoding UTF8
Set-Content -LiteralPath (Join-Path $OutputDir 'Program.cs') -Value $code -Encoding UTF8
dotnet build (Join-Path $OutputDir 'Fixture.csproj') -c Release *> (Join-Path $OutputDir 'build.log')
if($LASTEXITCODE -ne 0){Get-Content (Join-Path $OutputDir 'build.log') -Tail 14;exit $LASTEXITCODE}
dotnet (Join-Path $OutputDir 'bin\Release\net10.0-windows10.0.19041.0\Fixture.dll') $HostDll (Join-Path $OutputDir 'report.json')
exit $LASTEXITCODE

