// Extract only the real admission prefix before SDK construction. The gate and
// machine fence are injected ports; the real mutex/fence consumer is covered by
// usbip_upgrade_selftest.mjs, and native replay is covered by its native fixture.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const repo=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]??'');
assert(process.argv[2]&&!fs.existsSync(out),'Supply fresh external evidence');assert(!out.startsWith(repo+path.sep));fs.mkdirSync(out,{recursive:true});
const file=path.join(repo,'InputHost/Program.cs'),source=fs.readFileSync(file,'utf8'),hash=crypto.createHash('sha256').update(source).digest('hex');
const start=source.indexOf('        private bool TryPrepare(CommandEnvelope envelope, out string detail)'),end=source.indexOf('            if (_sdkDisposeFailure.Length != 0)',start);
assert(start>=0&&end>start,'Actual prepare prefix missing');const prefix=source.slice(start,end).replace('private bool TryPrepare','internal bool TryPrepare');
const program=`using System.Text.Json;
internal static class Program {
 static int checks;static void Need(bool value,string detail){if(!value)throw new Exception(detail);checks++;}
 static int Main(){
  var owner=new Consumer();
  foreach(string persona in new[]{"steamdeck","elite"}){
   YeManInputHost.UsbipMaintenanceGate.Admit=false;YeManCC.UsbipMaintenanceState.Reads=0;YeManCC.UsbipMaintenanceState.PendingValue=false;YeManCC.UsbipMaintenanceState.FailReads=persona!="steamdeck";
   Need(!owner.TryPrepare(new(persona),out var detail),"busy preparation crossed SDK boundary");Need(detail=="usbip-version-maintenance-in-progress","busy reason not eligible for native defer");Need(YeManCC.UsbipMaintenanceState.Reads==(persona=="steamdeck"?1:0),"non USB/IP persona probed system reboot state");
  }
  YeManCC.UsbipMaintenanceState.FailReads=false;YeManCC.UsbipMaintenanceState.PendingValue=true;
  Need(!owner.TryPrepare(new("steamdeck"),out var reboot)&&reboot=="usbip-reboot-required","mandatory reboot was collapsed into transient contention");Need(owner.SdkBoundary==0,"blocked state constructed SDK");
  YeManInputHost.UsbipMaintenanceGate.Admit=true;YeManCC.UsbipMaintenanceState.FailReads=true;
  Need(owner.TryPrepare(new("elite"),out _)&&owner.SdkBoundary==1,"granted lease did not reach the original SDK boundary");
  Need(YeManInputHost.UsbipMaintenanceGate.Disposed==1,"admission lease was not released");
  Console.WriteLine(JsonSerializer.Serialize(new{status="PASS",checks,hardwareIo=false,consumer="actual InputHost.TryPrepare admission prefix",ports="inert gate and pending-state reads only"}));return 0;
 }
}
internal sealed record CommandEnvelope(string Persona);
internal sealed class Consumer { internal int SdkBoundary;
${prefix}
            detail="fixture-sdk-boundary";SdkBoundary++;return true;
        }
}
namespace YeManInputHost {internal sealed class UsbipMaintenanceGate:IDisposable {internal static bool Admit;internal static int Disposed;internal static UsbipMaintenanceGate? TryEnter(bool required)=>Admit?new():null;public void Dispose()=>Disposed++;}}
namespace YeManCC {internal static class UsbipMaintenanceState {internal static bool PendingValue,FailReads;internal static int Reads;internal static bool Pending {get{Reads++;if(FailReads)throw new Exception("unnecessary pending-state read");return PendingValue;}}}}
`;
fs.writeFileSync(path.join(out,'Program.cs'),program);fs.writeFileSync(path.join(out,'PrefixTests.csproj'),'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup></Project>');
for(const [name,args]of [['build',['build',path.join(out,'PrefixTests.csproj'),'-o',path.join(out,'bin'),'-v:q']],['run',[path.join(out,'bin/PrefixTests.dll')]]]){const r=spawnSync('dotnet',args,{encoding:'utf8',windowsHide:true,timeout:30000});fs.writeFileSync(path.join(out,name+'.log'),(r.stdout??'')+(r.stderr??''));assert.equal(r.status,0,name+': '+r.stdout+r.stderr);if(name==='run'){const result=JSON.parse(r.stdout.trim());result.sourceSha256=hash;assert.equal(crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'),hash,'Source changed during admission test');fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));console.log(r.stdout.trim());}}
