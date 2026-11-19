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
    if($matches.Count -ne 1){throw "Expected one reviewed source fragment: $Pattern"}
    $matches[0].Value
}
$p=Read-Pin 'native/BatteryHost/Providers.cs'
$g=Read-Pin 'native/BatteryHost/SuperXChargeProvider.cs'
$policy=Read-Pin 'native/BatteryHost/Policy.cs'
$routes=Read-Pin 'native/BatteryHost/battery-routes.json'
$api=Project $p.text '(?ms)^internal sealed record Reading\([^\r\n]+;\r?\ninternal interface IChargeProvider : IDisposable\r?\n\{.*?^\}'
$guard=Project $g.text '(?m)^internal interface IChargeWriteAdmission \{[^\r\n]+\}'
$transport=Project $p.text '(?ms)^internal interface IEcAccess : IDisposable\r?\n\{.*?(?=^internal sealed class DeckProvider )'
$routeType=Project $policy.text '(?ms)^internal sealed record Route\([^\r\n]+\r?\n\{.*?^\}'
$allowed=Project $policy.text '(?m)^    public static bool ValueAllowed\([^\r\n]+;'
$expected=[ordered]@{
    'oxp-x1-amd-ec'=@(4,$true,0x04a3,80,85); 'oxp-x1-intel-ec'=@(1,$true,0x04a3,80,85)
    'oxp-x2-mini-pro-direct-ec'=@(1,$true,0x04e5,80,85); 'gpd-win-mini-2025'=@(1,$false,0x1d,80,85)
    'ayaneo-cec-bypass'=@(16,$false,0x1e,0xaa,0x55); 'ayaneo-ceii-bypass'=@(5,$true,0xd1d1,0x65,1)
}

# Retain the provider, access code and transaction methods. Substitute every
# external endpoint (mutex, LPC, ACPI and sleep) in the compilation namespace.
$source=@"
#nullable enable
using System;
using System.IO;
using System.Linq;
using AbandonedMutexException = System.Threading.AbandonedMutexException;
namespace BatteryEcEdgeAudit {
$api
$guard
$transport
internal sealed record MatchRule;
internal sealed record FirmwareGate;
$routeType
internal static class Policy {
$allowed
}
"@ + @'

internal sealed class Probe(string mode,ushort register,int raw,bool direct) {
    [ThreadStatic] internal static Probe Current=null!;
    public bool Revoked,PreReadComplete,BusyInjected;
    public int Gates,Acquired,Released,MutexClosed,Opened,Closed,Slots,UnlockBytes,ThresholdReads,ThresholdWrites,DataWritesLate,Commands81,Commands81Late;
    public int LateEndpoints,LatePorts,LateThresholdReads,SimulatedSleeps;
    public int Raw=raw;
    public void Check() {Gates++;if(Revoked)throw new IOException("inert admission revoked");}
    public void Grant(string name,int timeout) {
        if(timeout!=250||name!=(direct?@"Global\Access_ISABUS.HTP.Method":@"Global\Access_EC"))throw new InvalidOperationException("Unexpected bus contract");
        Acquired++;if(mode=="at-bus-grant")Revoked=true;
    }
    public void Open(bool kind) {if(kind!=direct)throw new InvalidOperationException("Wrong endpoint");Opened++;if(Revoked)LateEndpoints++;}
    public void SeenPort(){if(Revoked)LatePorts++;}
    public byte Read(ushort at) {
        if(at!=register)throw new InvalidOperationException($"Unexpected EC register {at:X4}");
        PreReadComplete=true;ThresholdReads++;if(Revoked)LateThresholdReads++;
        return (byte)(mode=="changed-raw"?Raw^1:Raw);
    }
    public void Commit(ushort at,byte value) {
        if(at!=register)throw new InvalidOperationException($"Unexpected EC register {at:X4}");
        ThresholdWrites++;if(Revoked)DataWritesLate++;Raw=value;
    }
    public bool BusyBeforeWrite() {
        if(!PreReadComplete||mode!="acpi-before-command"||BusyInjected)return false;
        if(Gates!=1)throw new InvalidOperationException("ACPI stimulus was not after provider admission");
        BusyInjected=true;Revoked=true;return true;
    }
}
internal sealed class Mutex(bool initial,string name) : IDisposable {
    public bool WaitOne(int ms){if(initial)throw new InvalidOperationException("Unexpected mutex ownership");Probe.Current.Grant(name,ms);return true;}
    public void ReleaseMutex()=>Probe.Current.Released++;
    public void Dispose()=>Probe.Current.MutexClosed++;
}
internal static class Thread {
    public static void Sleep(int ms) {
        if(ms!=1)throw new InvalidOperationException("Unexpected wait interval");
        Probe.Current.SimulatedSleeps++;
    }
}
internal sealed class LpcIo {
    readonly byte[] unlock=[0x87,0x01,0x55,0xaa];
    int unlockPos,idx,ldn;
    ushort addr;
    public LpcIo()=>Probe.Current.Open(true);
    public void SelectSlot(int slot){if(slot!=1)throw new InvalidOperationException("Unexpected LPC slot");Probe.Current.Slots++;}
    public void WritePort(ushort port,byte value) {
        var io=Probe.Current;io.SeenPort();
        if(unlockPos<unlock.Length){if(port!=0x4e||value!=unlock[unlockPos++])throw new InvalidOperationException("Wrong LPC unlock");io.UnlockBytes++;return;}
        if(port==0x4e){idx=value;return;}
        if(port!=0x4f)throw new InvalidOperationException("Unexpected LPC port");
        if(idx==0x2e){ldn=value;return;}
        if(idx!=0x2f)throw new InvalidOperationException("Unexpected LPC selector");
        if(ldn==0x11){addr=(ushort)((addr&255)|(value<<8));return;}
        if(ldn==0x10){addr=(ushort)((addr&0xff00)|value);return;}
        if(ldn!=0x12)throw new InvalidOperationException("Unexpected LPC data destination");
        io.Commit(addr,value);
    }
    public byte ReadPort(ushort port) {
        if(port!=0x4f||idx!=0x2f||ldn!=0x12)throw new InvalidOperationException("Unexpected LPC read");
        var io=Probe.Current;io.SeenPort();return io.Read(addr);
    }
    public void Close()=>Probe.Current.Closed++;
}
internal sealed class WindowsEmbeddedControllerIO : IDisposable {
    internal enum Port { Command, Data }
    int phase;
    ushort addr;
    public WindowsEmbeddedControllerIO()=>Probe.Current.Open(false);
    public byte ReadIOPort(Port port) {
        var io=Probe.Current;io.SeenPort();
        if(port==Port.Command){if(phase==0&&io.BusyBeforeWrite())return 2;return phase==2?(byte)1:(byte)0;}
        if(phase!=2)throw new InvalidOperationException("Unexpected ACPI data read");
        phase=0;return io.Read(addr);
    }
    public void WriteIOPort(Port port,byte value) {
        var io=Probe.Current;io.SeenPort();
        if(port==Port.Command) {
            if(phase!=0)throw new InvalidOperationException("ACPI command already active");
            if(value==0x80){phase=1;return;}
            if(value!=0x81)throw new InvalidOperationException("Unexpected ACPI command");
            phase=3;io.Commands81++;if(io.Revoked)io.Commands81Late++;return;
        }
        if(phase==1||phase==3){addr=value;phase++;return;}
        if(phase!=4)throw new InvalidOperationException("Unexpected ACPI data write");
        phase=0;io.Commit(addr,value);
    }
    public void Dispose()=>Probe.Current.Closed++;
}
public sealed record Observation(string Route,string Case,bool Direct,int Register,string Failure,int Gates,int Acquired,int Released,int MutexClosed,
    int Endpoints,int EndpointsClosed,int LateEndpoints,int Slots,int UnlockBytes,int LatePorts,int ThresholdReads,int LateThresholdReads,
    int ThresholdWrites,int LateThresholdWrites,int Commands81,int LateCommands81,int SimulatedSleeps,int FinalRaw);
public static class Audit {
    public static Observation Run(string id,string kind,int min,bool direct,int addr,int raw,int planned,string mode) {
        var io=Probe.Current=new(mode,(ushort)addr,raw,direct);
        using var provider=new EcProvider(new(id,"inert",kind,new(),new(),min));
        string failure="";
        try{provider.Write(kind=="software-bypass-threshold"?1:planned,new(raw,raw),io.Check);}
        catch(IOException e){failure=e.Message;}
        return new(id,mode,direct,addr,failure,io.Gates,io.Acquired,io.Released,io.MutexClosed,io.Opened,io.Closed,io.LateEndpoints,io.Slots,
            io.UnlockBytes,io.LatePorts,io.ThresholdReads,io.LateThresholdReads,io.ThresholdWrites,io.DataWritesLate,io.Commands81,io.Commands81Late,io.SimulatedSleeps,io.Raw);
    }
}
}
'@
Add-Type -TypeDefinition $source
$all=@($routes.text | ConvertFrom-Json)
$samples=foreach($id in $expected.Keys) {
    $count,$direct,$addr,$raw,$planned=$expected[$id]
    $group=@($all | Where-Object route -CEQ $id)
    if($group.Count -ne $count -or @($group.kind | Sort-Object -Unique).Count -ne 1 -or @($group.min | Sort-Object -Unique).Count -ne 1){throw "Unexpected registered route: $id"}
    $modes=@('stable','changed-raw','at-bus-grant')
    if(!$direct){$modes+='acpi-before-command'}
    foreach($mode in $modes){
        $x=[BatteryEcEdgeAudit.Audit]::Run($id,$group[0].kind,$group[0].min,$direct,$addr,$raw,$planned,$mode)
        if($x.Acquired -ne $x.Released -or $x.Acquired -ne $x.MutexClosed -or $x.Endpoints -ne $x.EndpointsClosed){throw "Unbalanced inert owner: $id / $mode"}
        if($mode -ceq 'stable' -and ($x.Failure -or $x.FinalRaw -ne $planned -or $x.Gates -ne 1 -or $x.ThresholdWrites -ne 1)){throw "Wrong normal transaction: $id"}
        if($mode -ceq 'changed-raw' -and (!$x.Failure -or $x.FinalRaw -ne $raw -or $x.Gates -ne 0 -or $x.ThresholdWrites -ne 0)){throw "Wrong external-change rejection: $id"}
        if($mode -ceq 'at-bus-grant' -and $x.ThresholdWrites -ne 0){throw "Existing post-read guard unexpectedly lost: $id"}
        if($mode -ceq 'acpi-before-command' -and $x.SimulatedSleeps -ne 1){throw "ACPI stimulus not reached: $id"}
        $x
    }
}
$lateOpen=@($samples | Where-Object LateEndpoints -GT 0)
$lateWrite=@($samples | Where-Object LateThresholdWrites -GT 0)
$summary=[ordered]@{schemaVersion=1;scope='Production EcProvider, EcAccess and Route/value predicate; in-memory substitutes for all external interfaces. New user firmware write or software pause only, not existing-owner restore.';
    inputs=[ordered]@{tool=(Read-Pin 'tools/battery-ec-edge-audit.ps1').pin;providers=$p.pin;guard=$g.pin;policy=$policy.pin;routes=$routes.pin};
    registerGroups=$expected.Count;identityRules=($expected.Values | ForEach-Object {$_[0]} | Measure-Object -Sum).Sum;
    lateOpenCases=$lateOpen.Count;lateThresholdWriteCases=$lateWrite.Count;samples=$samples}
if($Out){$summary | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath $Out -Encoding utf8}
$samples | Format-Table Route,Case,LateEndpoints,LatePorts,LateThresholdReads,LateCommands81,LateThresholdWrites
Write-Output "Late endpoint openings: $($lateOpen.Count). Late threshold writes: $($lateWrite.Count). No real port, mutex or sleep actions."
if($lateOpen.Count -or $lateWrite.Count){exit 1}
