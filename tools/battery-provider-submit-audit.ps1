param([string]$Out)

$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
function Read-Pin([string]$Relative) {
    $bytes=[IO.File]::ReadAllBytes((Join-Path $repo $Relative))
    [pscustomobject]@{text=[Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xFEFF);
        pin=[ordered]@{path=$Relative;sha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()}}
}
function Project([string]$Text,[string]$Pattern) {
    $matches=[regex]::Matches($Text,$Pattern)
    if($matches.Count -ne 1) { throw "Expected one reviewed source fragment: $Pattern" }
    $matches[0].Value
}
$controller=Read-Pin 'native/BatteryHost/Controller.cs'
$provider=Read-Pin 'native/BatteryHost/Providers.cs'
$guard=Read-Pin 'native/BatteryHost/SuperXChargeProvider.cs'
$routes=Read-Pin 'native/BatteryHost/battery-routes.json'
$ops=@($routes.text | ConvertFrom-Json)
$lenovoRows=@($ops | Where-Object route -ceq 'lenovo-go-conservation')
$deckRows=@($ops | Where-Object route -ceq 'valve-deck-firmware')
if($lenovoRows.Count -ne 4 -or @($lenovoRows | Where-Object kind -cne 'firmware-fixed').Count -ne 0 -or
    $deckRows.Count -ne 1 -or $deckRows[0].kind -cne 'firmware-percent' -or
    !($deckRows[0].firmwareGate.pairs | Where-Object {$_.boardId -eq 10 -and $_.firmware -eq 0xb030})) {
    throw 'The reviewed production routes changed; update the fixture after source review.'
}
$api=Project $provider.text '(?ms)^internal sealed record Reading\([^\r\n]+;\r?\ninternal interface IChargeProvider : IDisposable\r?\n\{.*?^\}'
$guardApi=Project $guard.text '(?m)^internal interface IChargeWriteAdmission \{[^\r\n]+\}'
$lenovo=Project $provider.text '(?ms)^internal sealed class LenovoProvider : [^\r\n]+\r?\n\{.*?^\}'
$deck=Project $provider.text '(?ms)^internal sealed class DeckProvider : [^\r\n]+\r?\n\{.*?^\}'
$deckHeader=Project $deck '(?m)^internal sealed class DeckProvider : [^\r\n]+(?=\r?$)'
$deckOps=Project $deck '(?ms)^    void CheckFirmware\(\)\r?\n    \{.*?(?=^    public void Dispose\()'

# Compile the full Controller and Lenovo provider, and the Deck probe/read/write
# methods. Replace native constructors, calls and storage with in-memory endpoints.
$source=@"
#nullable enable
global using System;
global using System.IO;
global using System.Linq;
global using System.Collections.Generic;
$($controller.text.Replace('namespace YeManBatteryHost;', 'namespace YeManBatteryHost {'))
$api
$guardApi
$lenovo
$deckHeader
{
    public bool CanReadLimit=>true;
    byte ReadByte(long address)=>Inert.Current.ReadDeck(address);
    IntPtr map(IntPtr address,uint size,out IntPtr handle)=>Inert.Current.Map(address,size,out handle);
    bool unmap(IntPtr handle,IntPtr ptr)=>Inert.Current.Unmap(handle,ptr);
$deckOps
    public void Dispose()=>Inert.Current.Close();
}
"@ + @'

internal sealed record Identity(string Key,string Manufacturer,string BoardProduct,string SystemProduct,string Bios,int EcMajor,int EcMinor);
internal sealed record MatchRule(string DeviceClass);
internal sealed record Route(string RouteId,string Family,string Kind,MatchRule Identity) {
    public bool Software=>false;
    public int[] Choices=>Kind=="firmware-fixed"?[80,100]:[80,90,100];
    public object FirmwareGate=>"inert";
}
internal static class Policy {
    public static System.Text.Json.JsonSerializerOptions Json {get;}=new(System.Text.Json.JsonSerializerDefaults.Web);
    public static YeManFanBackend.Capabilities.BatteryCapabilityBinding<Route> ResolveCapability(Identity id) =>
        new(new("audit-device","audit-rule","audit-only",false),id.Manufacturer=="LENOVO"
            ?new("lenovo-go-conservation","Legion Go","firmware-fixed",new("LegionGoTablet"))
            :new("valve-deck-firmware","Steam Deck","firmware-percent",new("SteamDeck")));
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
internal sealed class ManagementObject(Action? close=null) : IDisposable {
    readonly Dictionary<string,object> fields=new();
    public object? this[string key] {get=>fields.GetValueOrDefault(key);set=>fields[key]=value!;}
    public void Dispose()=>close?.Invoke();
}
internal static class Wmi {
    public static ManagementObject First(string scope,string name) => scope==@"root\WMI"&&name=="LENOVO_OTHER_METHOD"
        ?new(Inert.Current.Close):throw new InvalidOperationException("Unexpected WMI endpoint");
    public static ManagementObject Call(ManagementObject instance,string method,Action<ManagementObject> bind) {
        using var input=new ManagementObject();bind(input);
        if(input["IDs"] is not uint id||id!=0x03010001u)throw new InvalidOperationException("Unexpected Lenovo selector");
        if(method=="GetFeatureValue")return new ManagementObject{["Value"]=Inert.Current.ReadLimit()};
        if(method!="SetFeatureValue"||input["value"] is not uint value||value>1)throw new InvalidOperationException("Unexpected Lenovo command");
        Inert.Current.Submit((int)value);return new ManagementObject();
    }
    public static void CheckReturn(ManagementObject output){}
}
internal static class Marshal {
    public static void WriteByte(IntPtr pointer,byte value) {
        if(pointer!=new IntPtr(1))throw new InvalidOperationException("Not an inert mapping");
        Inert.Current.Submit(value);
    }
}
internal sealed class Inert(string mode,int raw) {
    [ThreadStatic] internal static Inert Current=null!;
    public int Opens,Closes,Reads,Writes,Maps,Unmaps,LateWrites,LateReads;
    public bool Ready=true,Present=true,RequestCurrent=true,Revoked;
    public int Raw=raw;
    void Revoke() {
        Revoked=true;
        switch(mode) {
            case "ready-after-preread": case "ready-after-map": Ready=false;break;
            case "battery-after-preread": Present=false;break;
            case "expired-after-preread": RequestCurrent=false;break;
            default: throw new InvalidOperationException("Unexpected revocation stimulus");
        }
    }
    public int ReadLimit() {
        Reads++;if(Revoked)LateReads++;
        // 1: user GET; 2: Apply fence observation; 3: final provider pre-read.
        if(Reads==3&&mode.EndsWith("-after-preread",StringComparison.Ordinal))Revoke();
        if(Reads==3&&mode=="changed-raw")return Raw==1?0:Raw+5;
        return Raw;
    }
    public byte ReadDeck(long address)=>address switch {
        0xfe7003bd=>10,0xfe700c4c=>0x30,0xfe700c4d=>0xb0,0xfe700b9f=>(byte)ReadLimit(),
        _=>throw new InvalidOperationException("Unexpected Deck byte")
    };
    public IntPtr Map(IntPtr address,uint size,out IntPtr handle) {
        if(address.ToInt64()!=0xfe700b9fL||size!=1)throw new InvalidOperationException("Unexpected Deck write mapping");
        Maps++;if(mode=="ready-after-map")Revoke();handle=new IntPtr(2);return new IntPtr(1);
    }
    public bool Unmap(IntPtr handle,IntPtr pointer) {
        if(handle!=new IntPtr(2)||pointer!=new IntPtr(1))throw new InvalidOperationException("Unexpected inert mapping return");
        Unmaps++;return true;
    }
    public void Submit(int value){Writes++;if(Revoked)LateWrites++;Raw=value;}
    public void Close()=>Closes++;
}
public sealed record Observation(string Route,string Case,bool Ok,string Reason,int LimitReads,int Opens,int Closes,int Maps,int Unmaps,
    int Writes,int WritesAfterRevocation,int LimitReadsAfterRevocation,int FinalRaw,bool FirmwareDebtRetained);
public static class SubmitAudit {
    public static Observation Run(string route,string mode) {
        bool lenovo=route=="lenovo-go-conservation";
        if(!lenovo&&route!="valve-deck-firmware")throw new ArgumentException("Unknown audit route");
        int target=lenovo?100:90,initialRaw=lenovo?1:80;
        var io=Inert.Current=new(mode,initialRaw);
        var store=new MemoryStore();
        var id=new Identity("audit-only",lenovo?"LENOVO":"Valve",lenovo?"LNVNB161216":"Jupiter",lenovo?"83E1":"Jupiter","audit",1,40);
        var controller=new Controller(id,store,r=>{
            if(r.RouteId!=route)throw new InvalidOperationException("Inert projection selected the wrong route");
            io.Opens++;
            return lenovo?new LenovoProvider():new DeckProvider();
        },()=> (io.Present,true,75),()=>io.Ready,()=>io.RequestCurrent);
        var before=controller.Read();
        if(!before.Ok||before.CurrentLimit!=80||io.Reads!=1||io.Writes!=0)throw new InvalidOperationException("Missing initial read");
        var after=controller.Apply(before.Fence,target);
        var debt=store.Load<Recovery>(Controller.RecoveryFile);
        return new(route,mode,after.Ok,after.Reason,io.Reads,io.Opens,io.Closes,io.Maps,io.Unmaps,io.Writes,io.LateWrites,io.LateReads,io.Raw,
            debt is {Software:false,OwnedRaw:null}&&debt.OriginalRaw==initialRaw&&debt.RouteId==route);
    }
}
}
namespace YeManFanBackend.Capabilities {
    internal sealed record Descriptor(string DeviceId,string RuleId,string Origin,bool DeclaredByFrozenHc);
    internal sealed record BatteryCapabilityBinding<T>(Descriptor Descriptor,T Consumer);
    internal static class HcBatteryCapabilityCatalog { public const string CatalogSha256="inert projection"; }
}
'@
Add-Type -TypeDefinition $source

$observations=foreach($route in @('lenovo-go-conservation','valve-deck-firmware')) {
    $modes=@('stable','changed-raw','ready-after-preread','battery-after-preread','expired-after-preread')
    if($route -ceq 'valve-deck-firmware') { $modes+='ready-after-map' }
    foreach($mode in $modes) { [YeManBatteryHost.SubmitAudit]::Run($route,$mode) }
}
foreach($sample in $observations) {
    if($sample.Opens -ne $sample.Closes -or $sample.Maps -ne $sample.Unmaps -or
        ($sample.Case -ceq 'stable' -and (!$sample.Ok -or $sample.Writes -ne 1 -or $sample.LimitReads -ne 4 -or $sample.FirmwareDebtRetained)) -or
        ($sample.Case -cne 'stable' -and ($sample.Ok -or !$sample.FirmwareDebtRetained)) -or
        ($sample.Case -ceq 'changed-raw' -and $sample.Writes -ne 0)) {
        throw "Admission or return control did not reach expected branch: $($sample.Route) / $($sample.Case)"
    }
}
$revoked=@($observations | Where-Object Case -Like '*-after-*')
$late=@($revoked | Where-Object WritesAfterRevocation -GT 0)
$result=[ordered]@{schemaVersion=1;scope='Full production Controller and Lenovo provider plus exact Deck firmware/read/write methods, with inert identity, storage, WMI, mapping and byte I/O.';
    inputs=[ordered]@{tool=(Read-Pin 'tools/battery-provider-submit-audit.ps1').pin;controller=$controller.pin;providers=$provider.pin;writeGuard=$guard.pin;routes=$routes.pin};
    controls=4;revokedCases=$revoked.Count;lateSubmissionCases=$late.Count;observations=$observations}
if($Out) { $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Out -Encoding utf8 }
$observations | Format-Table Route,Case,Ok,Writes,WritesAfterRevocation,LimitReadsAfterRevocation,Maps,Unmaps
Write-Output "Writes after revocation: $($late.Count) of $($revoked.Count) cases. Controls: 4. No hardware I/O."
if($late.Count) { exit 1 }
