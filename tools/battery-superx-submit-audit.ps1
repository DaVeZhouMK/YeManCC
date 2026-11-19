param([string]$Out)

$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
function Read-Pin([string]$Relative) {
    $b=[IO.File]::ReadAllBytes((Join-Path $repo $Relative))
    [pscustomobject]@{text=[Text.Encoding]::UTF8.GetString($b).TrimStart([char]0xFEFF);
        pin=[ordered]@{path=$Relative;sha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($b)).ToLowerInvariant()} }
}
$provider=Read-Pin 'native/BatteryHost/SuperXChargeProvider.cs'
$writer=Read-Pin 'native/BatteryHost/SuperXCheckedEcWriter.cs'
$api=Read-Pin 'native/BatteryHost/Providers.cs'
$routes=Read-Pin 'native/BatteryHost/battery-routes.json'
$overlay=Read-Pin 'native/BatteryHost/superx-charge-route.json'
$rows=@(($routes.text | ConvertFrom-Json) | Where-Object route -CEQ 'oxp-superx-dual-ec')
$facts=$overlay.text | ConvertFrom-Json
if($rows.Count -ne 1 -or $rows[0].min -ne 75 -or $facts.limits.max -ne 100 -or $facts.limits.step -ne 5) {
    throw 'Unexpected reviewed Super X rule; do not reuse the audit without review.'
}
function Project([string]$Text,[string]$Pattern) {
    $matches=[regex]::Matches($Text,$Pattern)
    if($matches.Count -ne 1) { throw "Expected one reviewed source projection: $Pattern" }
    $matches[0].Value
}
$providerCode=Project $provider.text '(?ms)^internal interface IChargeWriteAdmission \{[^\r\n]+\}.*?^\}'
$writerCode=Project $writer.text '(?ms)^internal interface ISuperXChargeWriter [^\r\n]+.*?^internal interface ISuperXBusLease [^\r\n]+'
$writerCode+="`n"+(Project $writer.text '(?ms)^internal sealed class SuperXCheckedEcWriter : [^\r\n]+\r?\n\{.*?^\}')
$chargeApi=Project $api.text '(?ms)^internal sealed record Reading\([^\r\n]+;\r?\ninternal interface IChargeProvider : IDisposable\r?\n\{.*?^\}'

# Production provider and writer, including both original finally/cleanup paths.
# Injected endpoints cannot load drivers, open handles, or write ports.
$source=@"
#nullable enable
using System;
using System.IO;
using System.Collections.Generic;
using System.Runtime.ExceptionServices;
namespace BatterySuperXSubmitAudit {
$chargeApi
$providerCode
$writerCode
"@ + @'

internal interface ISuperXChargeReader : IDisposable { byte ReadLimit(); }
internal sealed class SuperXUmaChargeReader : ISuperXChargeReader {
    public byte ReadLimit()=>throw new InvalidOperationException("No native reader");
    public void Dispose(){}
}
internal sealed class SuperXCheckedLpcSession : ISuperXPortSession {
    public void SelectSlot(int slot)=>throw new InvalidOperationException("No native LPC session");
    public void WritePort(ushort port,byte value)=>throw new InvalidOperationException("No native port writer");
    public void Dispose(){}
}
internal sealed class SuperXIsaBusLease : ISuperXBusLease {
    public void Enter()=>throw new InvalidOperationException("No native ISA bus");
    public void Dispose(){}
}
internal sealed class Probe(string mode) : ISuperXChargeReader {
    public int Reads,Checks,BusAcquisitions,BusReturns,Slots,SessionCloses,ReaderCloses,LatePortOperations;
    public bool Revoked,StoredAfterRevocation;
    public byte Raw=80;
    public readonly List<PortOp> Ports=new();
    public byte ReadLimit(){if(++Reads==2&&mode=="uma-return")Revoked=true;return Raw;}
    public void Check(){Checks++;if(Revoked)throw new IOException("inert generation revoked");}
    public void Enter(){BusAcquisitions++;if(mode=="isa-acquired")Revoked=true;}
    public void SelectSlot(int slot){if(slot!=1)throw new InvalidOperationException("Wrong slot");Slots++;if(mode=="slot-prepared")Revoked=true;}
    public void WritePort(ushort port,byte value) {
        Ports.Add(new(port,value,Revoked));if(Revoked)LatePortOperations++;
        // The preset data position is part of the existing fixed source sequence.
        if(Ports.Count==16&&port==0x4f){Raw=value;StoredAfterRevocation=Revoked;}
    }
    public void Dispose()=>ReaderCloses++;
}
internal sealed class Lease(Probe probe) : ISuperXBusLease {
    public void Enter()=>probe.Enter();
    public void Dispose()=>probe.BusReturns++;
}
internal sealed class PortSession(Probe probe) : ISuperXPortSession {
    public void SelectSlot(int slot)=>probe.SelectSlot(slot);
    public void WritePort(ushort port,byte value)=>probe.WritePort(port,value);
    public void Dispose()=>probe.SessionCloses++;
}
public sealed record PortOp(ushort Port,byte Value,bool AfterRevocation);
public sealed record Observation(string Case,string Failure,int Reads,int Checks,int Buses,int BusReturns,int Slots,int SessionCloses,
    int ReaderCloses,int PortOperations,int LatePortOperations,int FinalRaw,bool StoredAfterRevocation,PortOp[] Trace);
public static class Audit {
    public static Observation Run(string mode) {
        var probe=new Probe(mode);
        string result="";
        using(var p=new SuperXChargeProvider(probe,new SuperXCheckedEcWriter(()=>new PortSession(probe),()=>new Lease(probe)))) {
            var before=p.Read();
            try {p.Write(85,before,probe.Check);}catch(IOException e){result=e.Message;}
        }
        return new(mode,result,probe.Reads,probe.Checks,probe.BusAcquisitions,probe.BusReturns,probe.Slots,probe.SessionCloses,
            probe.ReaderCloses,probe.Ports.Count,probe.LatePortOperations,probe.Raw,probe.StoredAfterRevocation,probe.Ports.ToArray());
    }
}
}
'@
Add-Type -TypeDefinition $source
$observations=@('stable','uma-return','isa-acquired','slot-prepared') | ForEach-Object { [BatterySuperXSubmitAudit.Audit]::Run($_) }
$good=$observations[0]
$uma=$observations[1]
if($good.Failure -or $good.FinalRaw -ne 85 -or $good.PortOperations -ne 16 -or $good.Trace[-1].Port -ne 0x4f -or $good.Trace[-1].Value -ne 85 -or
    !$uma.Failure -or $uma.Buses -ne 0 -or $uma.Slots -ne 0 -or $uma.PortOperations -ne 0 -or $uma.FinalRaw -ne 80) {
    throw 'Normal path or existing guard control did not reach the expected boundary.'
}
foreach($x in $observations) {
    if($x.Buses -ne $x.BusReturns -or $x.Slots -ne $x.SessionCloses -or $x.ReaderCloses -ne 1) { throw "Unbalanced inert owner: $($x.Case)" }
}
$late=@($observations | Where-Object {$_.LatePortOperations -gt 0 -or $_.StoredAfterRevocation})
$result=[ordered]@{schemaVersion=1;scope='Exact Super X provider and checked writer with an inert guard and injected reader, ISA lease and port session; no Controller, native calls or hardware I/O.';
    inputs=[ordered]@{tool=(Read-Pin 'tools/battery-superx-submit-audit.ps1').pin;provider=$provider.pin;writer=$writer.pin;api=$api.pin;routes=$routes.pin;overlay=$overlay.pin};
    lateCases=$late.Count;observations=$observations}
if($Out) { $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Out -Encoding utf8 }
$observations | Format-Table Case,Checks,Buses,BusReturns,PortOperations,LatePortOperations,FinalRaw,StoredAfterRevocation
Write-Output "Super X late port operations in $($late.Count) cases; ordinary write and existing pre-read guard controls verified."
if($late.Count) { exit 1 }
