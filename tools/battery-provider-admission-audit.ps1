param([string]$Out)

$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$controllerPath=Join-Path $repo 'native/BatteryHost/Controller.cs'
$providersPath=Join-Path $repo 'native/BatteryHost/Providers.cs'
$guardPath=Join-Path $repo 'native/BatteryHost/SuperXChargeProvider.cs'
$routesPath=Join-Path $repo 'native/BatteryHost/battery-routes.json'

function Read-Pinned([string]$File) {
    $b=[IO.File]::ReadAllBytes($File)
    [pscustomobject]@{ text=[Text.Encoding]::UTF8.GetString($b).TrimStart([char]0xFEFF);
        pin=[ordered]@{path=$File;sha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($b)).ToLowerInvariant()} }
}
$controller=Read-Pinned $controllerPath
$providers=Read-Pinned $providersPath
$guard=Read-Pinned $guardPath
$routes=Read-Pinned $routesPath
$api=[regex]::Match($providers.text,'(?ms)^internal sealed record Reading\([^\r\n]+;\r?\ninternal interface IChargeProvider : IDisposable\r?\n\{.*?^\}')
$writeApi=[regex]::Match($guard.text,'(?m)^internal interface IChargeWriteAdmission \{[^\r\n]+\}')
if(!$api.Success -or !$writeApi.Success) { throw 'Provider contracts changed; review the projection.' }
$row=@(($routes.text | ConvertFrom-Json) | Where-Object route -CEQ 'oxp3-wmi-ec')
if($row.Count -ne 1 -or $row[0].kind -cne 'firmware-percent' -or $row[0].firmwareGate.kind -cne 'provider-probe-required') {
    throw 'The audited OP3 route no longer matches this stub contract.'
}

# Compile the complete real Controller.cs, plus only the real provider interfaces.
# Identity lookup, firmware admission, journal and the hardware endpoint are inert.
$source=@"
#nullable enable
global using System;
global using System.IO;
global using System.Linq;
global using System.Collections.Generic;
$($controller.text)
"@
$source=$source.Replace('namespace YeManBatteryHost;', 'namespace YeManBatteryHost {') + "`n" + $api.Value + "`n" + $writeApi.Value + @'

internal sealed record Identity(string Key,string Manufacturer,string BoardProduct,string SystemProduct,string Bios,int EcMajor,int EcMinor);
internal sealed record MatchRule(string DeviceClass);
internal sealed record Route(string RouteId,string Family,string Kind,MatchRule Identity) {
    public bool Software=>false;
    public int[] Choices=>[80,90,100];
    public object FirmwareGate=>"provider-probe-required";
}
internal static class Policy {
    public static System.Text.Json.JsonSerializerOptions Json {get;}=new(System.Text.Json.JsonSerializerDefaults.Web);
    public static YeManFanBackend.Capabilities.BatteryCapabilityBinding<Route> ResolveCapability(Identity id) =>
        new(new("audit-device","audit-rule","audit-only",false),new("oxp3-wmi-ec","OP3","firmware-percent",new("OneXPlayer3")));
    public static bool FirmwareAllowed(Route route,Identity id)=>true;
    public static bool ValueAllowed(Route route,int value)=>route.Choices.Contains(value);
    public static int? BypassDecision(int? percent,bool ac,int target,bool paused)=>throw new InvalidOperationException("Not a software test");
    public static string Unavailable(Identity id)=>"not selected";
}
internal static class WindowsFacts {
    public static (bool Present,bool Ac,int? Percent) Power()=>throw new InvalidOperationException("No hardware access");
}
internal static class TraceLog {
    public static void Write(string e,object detail){}
    public static void Error(string e,Exception error,object? detail=null){}
}
internal sealed class MemoryStore : Store {
    readonly Dictionary<string,object> values=new();
    public MemoryStore():base("audit-memory-only"){}
    public override T? Load<T>(string name) where T:default=>values.TryGetValue(name,out var v)?(T)v:default;
    public override void Save<T>(string name,T value)=>values[name]=value!;
    public override void Clear(string name)=>values.Remove(name);
}
internal sealed class InertProvider(Func<Reading> read,Action<int> write,Action afterPreread,Action close) : IChargeProvider,IChargeWriteAdmission {
    public bool CanReadLimit=>true;
    public Reading Read()=>read();
    public void Write(int value,Reading before)=>throw new InvalidOperationException("Expected guarded write");
    public void Write(int value,Reading before,Action requireAvailable) {
        _=Read();
        afterPreread();
        requireAvailable();
        write(value);
    }
    public void Dispose()=>close();
}
public sealed record Observation(string Case,bool Ok,string Reason,int Opens,int Reads,int Writes,int Closes,
    int OpensAfterRevocation,int ReadsAfterRevocation,bool FirmwareDebtRetained);
public static class Audit {
    public static Observation Run(string mode) {
        var store=new MemoryStore();
        int opens=0,reads=0,writes=0,closes=0,lateOpens=0,lateReads=0,limit=80;
        bool ready=true,present=true,current=true,revoked=false;
        Controller? owner=null;
        owner=new(new("pinned-test-identity","ONE-NETBOOK","board","ONEXPLAYER 3","2.10",0,12),store,
            _=>{
                opens++;if(revoked)lateOpens++;
                return new InertProvider(
                    ()=>{reads++;if(revoked)lateReads++;return new(limit,limit);},
                    value=>{writes++;if(mode=="transport-rejection")throw new IOException("inert transport rejection");limit=value;},
                    ()=>{
                        switch(mode) {
                            case "suspend": revoked=true;owner!.Suspend();break;
                            case "shutdown": revoked=true;owner!.Shutdown();break;
                            case "ready-lost": revoked=true;ready=false;break;
                            case "battery-absent": revoked=true;present=false;break;
                            case "request-expired": revoked=true;current=false;break;
                        }
                    },()=>closes++);
            },()=> (present,true,75),()=>ready,()=>current);
        var initial=owner.Read();
        if(!initial.Ok||initial.CurrentLimit!=80||opens!=1||reads!=1||writes!=0)throw new InvalidOperationException("Missing initial read");
        var after=owner.Apply(initial.Fence,90);
        var debt=store.Load<Recovery>(Controller.RecoveryFile);
        return new(mode,after.Ok,after.Reason,opens,reads,writes,closes,lateOpens,lateReads,
            debt is {Software:false,OriginalRaw:80,OwnedRaw:null}&&debt.RouteId=="oxp3-wmi-ec");
    }
}
}
namespace YeManFanBackend.Capabilities {
    internal sealed record Descriptor(string DeviceId,string RuleId,string Origin,bool DeclaredByFrozenHc);
    internal sealed record BatteryCapabilityBinding<T>(Descriptor Descriptor,T Consumer);
    internal static class HcBatteryCapabilityCatalog { public const string CatalogSha256="not-a-catalog-test"; }
}
'@
Add-Type -TypeDefinition $source
$observations=@('stable','transport-rejection','suspend','shutdown','ready-lost','battery-absent','request-expired') | ForEach-Object { [YeManBatteryHost.Audit]::Run($_) }
$stable=$observations[0]
$rejected=$observations[1]
if(!$stable.Ok -or $stable.Writes -ne 1 -or $stable.FirmwareDebtRetained -or $rejected.Ok -or $rejected.Writes -ne 1 -or !$rejected.FirmwareDebtRetained) {
    throw 'Stable or transport-rejection control did not reach the intended branch.'
}
foreach($sample in ($observations | Select-Object -Skip 2)) {
    if($sample.Ok -or $sample.Writes -ne 0 -or $sample.Closes -ne $sample.Opens -or !$sample.FirmwareDebtRetained) {
        throw "Admission/return test control failed: $($sample.Case)"
    }
}
$findings=@($observations | Where-Object {$_.OpensAfterRevocation -ne 0 -or $_.ReadsAfterRevocation -ne 0})
$result=[ordered]@{schemaVersion=1;scope='Complete Controller source with inert route resolution, journal and provider; no native Host/OS/device actions';
    inputs=@{tool=(Read-Pinned $PSCommandPath).pin;controller=$controller.pin;providers=$providers.pin;writeGuard=$guard.pin;routes=$routes.pin};
    controls=2;revokedCases=5;reopenedAfterRevocation=$findings.Count;observations=$observations}
if($Out) { $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Out -Encoding utf8 }
$observations | Format-Table Case,Ok,Opens,Reads,Writes,Closes,OpensAfterRevocation,ReadsAfterRevocation,FirmwareDebtRetained
Write-Output "Post-revocation endpoint opens or reads in $($findings.Count) of 5 cases."
if($findings.Count) { exit 1 }
