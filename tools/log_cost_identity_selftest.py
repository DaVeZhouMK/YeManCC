"""Compile actual Host log identity initializer fragments with inert session values."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);p.add_argument('--baseline-host',required=True,type=Path);args=p.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
 program=(ROOT/'InputHost/Program.cs').read_text(encoding='utf-8-sig').replace('\r\n','\n');msi=(ROOT/'InputHost/MsiCostWindowDiagnostics.cs').read_text(encoding='utf-8-sig').replace('\r\n','\n')
 def region(s,start,end):
  a=s.index(start);b=s.index(end,a);return s[a:b]
 regular=region(program,'                        // Writer/session identity remains available','                        windowMs,')
 deferred=region(msi,'            // Use the captured window\'s session identity','            t = window.EpochMs,')
 cs='''using System;using System.Text.Json;using System.Collections.Generic;
record Session(string? RunId,ulong Epoch,ulong PowerGeneration);
record Window(string HostInstanceId,string? RunId,ulong Epoch,ulong PowerGeneration);
class Fixture {
static JsonElement Regular(string _hostInstanceId,Session? _tuple,object? transport) => JsonSerializer.SerializeToElement(new {
'''+regular+'''usbipTransport=transport });
static JsonElement Deferred(Window window,object? transport) => JsonSerializer.SerializeToElement(new {
'''+deferred+'''usbipTransport=transport });
static int Main(){var checks=new List<object>();int failed=0;
void Check(string name,bool pass){checks.Add(new{name,pass});if(!pass)failed++;}
var tuple=new Session("run-live",7,3);var window=new Window("host-captured","run-captured",2,1);
var a=Regular("host-live",tuple,null);var b=Deferred(window,null);
Check("regular-null-transport-writer-known",a.GetProperty("writerPid").GetInt32()==Environment.ProcessId);
Check("regular-null-transport-session-known",a.GetProperty("hostInstanceId").GetString()=="host-live"&&a.GetProperty("runId").GetString()=="run-live");
Check("regular-session-counters-retained",a.GetProperty("epoch").GetUInt64()==7&&a.GetProperty("powerGeneration").GetUInt64()==3);
Check("regular-null-transport-preserved",a.GetProperty("usbipTransport").ValueKind==JsonValueKind.Null);
Check("deferred-writer-is-emitting-process",b.GetProperty("writerPid").GetInt32()==Environment.ProcessId);
Check("deferred-uses-captured-host-run",b.GetProperty("hostInstanceId").GetString()=="host-captured"&&b.GetProperty("runId").GetString()=="run-captured");
Check("deferred-uses-captured-counters",b.GetProperty("epoch").GetUInt64()==2&&b.GetProperty("powerGeneration").GetUInt64()==1);
var unbound=Regular("host-new",null,null);Check("unbound-does-not-invent-session",unbound.GetProperty("runId").GetString()==""&&unbound.GetProperty("epoch").GetUInt64()==0&&unbound.GetProperty("powerGeneration").GetUInt64()==0);
Check("unbound-still-has-writer-host",unbound.GetProperty("writerPid").GetInt32()==Environment.ProcessId&&unbound.GetProperty("hostInstanceId").GetString()=="host-new");
var other=Regular("host-live",tuple,new{hostPid=777,hostInstanceId="observed-other"});Check("transport-subject-does-not-replace-writer",other.GetProperty("writerPid").GetInt32()==Environment.ProcessId&&other.GetProperty("hostInstanceId").GetString()=="host-live"&&other.GetProperty("usbipTransport").GetProperty("hostPid").GetInt32()==777);
Check("session-and-captured-values-not-mutated",tuple==new Session("run-live",7,3)&&window==new Window("host-captured","run-captured",2,1));
var unknown=Deferred(new Window("host-a",null,0,0),null);Check("deferred-null-run-remains-unbound",unknown.GetProperty("runId").GetString()=="");
Console.WriteLine(JsonSerializer.Serialize(new{checks,failures=failed,scope="source-extracted identity fields; fixture values; no device or product"}));return failed==0?0:1;}}
'''
 (out/'Fixture.cs').write_text(cs,encoding='utf-8')
 (out/'Fixture.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup><ItemGroup><Compile Include="Fixture.cs" /></ItemGroup></Project>',encoding='utf-8')
 result=subprocess.run(['dotnet','build',str(out/'Fixture.csproj'),'-c','Release','--nologo','--ignore-failed-sources'],capture_output=True,text=True,encoding='utf-8',errors='replace');(out/'compile.log').write_text(result.stdout+result.stderr,encoding='utf-8')
 if result.returncode:print(result.stdout,result.stderr);return result.returncode
 result=subprocess.run(['dotnet',str(out/'bin/Release/net10.0/Fixture.dll')],capture_output=True,text=True,encoding='utf-8');(out/'result.json').write_text(result.stdout,encoding='utf-8')
 assert result.returncode==0,result.stdout+result.stderr
 # The changed code in both producers is strictly additive identity inside existing serialization.
 baseline=args.baseline_host.resolve()
 oldProgram=(baseline/'Program.cs').read_text(encoding='utf-8-sig').replace('\r\n','\n');oldMsi=(baseline/'MsiCostWindowDiagnostics.cs').read_text(encoding='utf-8-sig').replace('\r\n','\n')
 def method(s,start,end):return region(s,start,end)
 regularStable=method(program.replace(regular,''),'        private void MaybeEmitCostWindow()','        // Only bounded in-memory copies happen here.')==method(oldProgram,'        private void MaybeEmitCostWindow()','        // Only bounded in-memory copies happen here.')
 msiStable=msi.replace(deferred,'')==oldMsi
 assert regularStable and msiStable,'producer changed beyond the identity initializer; review drift'
 receipt={'checks':len(json.loads(result.stdout)['checks']),'failures':0,'regularProducerUnchangedExceptIdentity':regularStable,'msiFileUnchangedExceptIdentity':msiStable,
 'regularIdentitySha256':hashlib.sha256(regular.encode()).hexdigest(),'deferredIdentitySha256':hashlib.sha256(deferred.encode()).hexdigest()}
 (out/'binding.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');print(json.dumps(receipt));return 0
if __name__=='__main__':raise SystemExit(main())
