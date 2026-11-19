param([string]$Out)

$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$providersPath=Join-Path $repo 'native/BatteryHost/Providers.cs'
$routesPath=Join-Path $repo 'native/BatteryHost/battery-routes.json'
$providersBytes=[IO.File]::ReadAllBytes($providersPath)
$routeBytes=[IO.File]::ReadAllBytes($routesPath)
$text=[Text.Encoding]::UTF8.GetString($providersBytes)
function Project([string]$pattern) {
    $matches=[regex]::Matches($text,$pattern)
    if($matches.Count -ne 1){throw "Expected one reviewed source projection: $pattern"}
    $matches[0].Value
}
$factory=Project '(?ms)^    public static IChargeProvider Open\(Route route,string powerRoot\)=>route\.RouteId switch\r?\n    \{.*?^    \};'
$ecMembers=@('internal const string X2MiniProRouteId','bool Direct','ushort Address','int Pause','int Resume') | ForEach-Object {
    Project ('(?m)^    '+[regex]::Escape($_)+'[^\r\n]+;')
}
$deckGate=Project '(?ms)^    void CheckFirmware\(\)\r?\n    \{.*?^    \}'

# For this equality-only gate, values absent from both inputs are equivalent.
# Verify the decoding and the whole predicate shape before relying on that partition.
$decode=[regex]::Escape('int board=ReadByte(0xfe7003bd),fw=ReadByte(0xfe700c4c)|(ReadByte(0xfe700c4d)<<8);')
$fwLiteral='(?<firmware>0[xX][0-9a-fA-F]{1,4})'
$term='board==(?<board>[0-9]{1,3})&&(?:fw=='+$fwLiteral+'|new\[\]\{'+$fwLiteral+'(?:,'+$fwLiteral+')*\}\.Contains\(fw\))'
$shape='\A    void CheckFirmware\(\)\r?\n    \{\r?\n        '+$decode+'\r?\n        if\(!\('+$term+'(?:\|\|'+$term+')*\)\)throw new IOException\("[^"\r\n]*"\);\r?\n    \}\z'
$consumerGate=[regex]::Match($deckGate,$shape,[Text.RegularExpressions.RegexOptions]::CultureInvariant)
if(!$consumerGate.Success){throw 'Unreviewed Deck pair predicate shape; verify the new decoding and accepted domain before comparing.'}
$consumerBoards=@($consumerGate.Groups['board'].Captures|ForEach-Object {[int]$_.Value}|Sort-Object -Unique)
$consumerFirmware=@($consumerGate.Groups['firmware'].Captures|ForEach-Object {[Convert]::ToInt32($_.Value.Substring(2),16)}|Sort-Object -Unique)
if(@($consumerBoards|Where-Object {$_ -gt 255}).Count){throw 'Deck predicate contains an invalid board byte'}

# Execute exact selector and gate source with inert constructor and byte-read stand-ins.
$code=@"
using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
namespace BatteryProviderBindingAudit {
public sealed record Route(string RouteId);
public interface IChargeProvider {}
sealed class AsusProvider : IChargeProvider {}
sealed class LenovoProvider : IChargeProvider {}
sealed class MsiProvider : IChargeProvider {}
sealed class MsiApProvider : IChargeProvider {}
sealed class Oxp3WmiProvider : IChargeProvider {}
sealed class SuperXChargeProvider : IChargeProvider {}
sealed class EcProvider(Route route) : IChargeProvider {
$($ecMembers -join "`n")
    public bool IsDirect=>Direct;
    public int Register=>Address;
    public int PauseValue=>Pause;
    public int ResumeValue=>Resume;
}
sealed class DeckProvider(string powerRoot) : IChargeProvider {
    int board,fw;
    byte ReadByte(long address) => address switch {
        0xfe7003bd => (byte)board, 0xfe700c4c => (byte)fw, 0xfe700c4d => (byte)(fw>>8),
        _ => throw new InvalidOperationException("Unexpected Deck probe byte")
    };
$deckGate
    public bool Allows(int boardId,int firmware) {
        if(powerRoot!="inert")throw new InvalidOperationException("Unexpected construction context");
        board=boardId;fw=firmware;
        try{CheckFirmware();return true;}catch(IOException){return false;}
    }
}
static class Providers {
$factory
}
public sealed record Selection(string Provider,bool? Direct,int? Register,int? Pause,int? Resume);
public static class Probe {
    public static Selection Resolve(string id) {
        var p=Providers.Open(new(id),"inert");
        return p is EcProvider ec ? new(p.GetType().Name,ec.IsDirect,ec.Register,ec.PauseValue,ec.ResumeValue) : new(p.GetType().Name,null,null,null,null);
    }
    public static bool DeckAllows(int board,int fw)=>((DeckProvider)Providers.Open(new("valve-deck-firmware"),"inert")).Allows(board,fw);
}}
"@
Add-Type -TypeDefinition $code

$routes=@([Text.Encoding]::UTF8.GetString($routeBytes).TrimStart([char]0xFEFF) | ConvertFrom-Json)
$expected=[ordered]@{
    'asus-ally-acpi'=@('AsusProvider'); 'lenovo-go-conservation'=@('LenovoProvider');
    'msi-claw-wmi'=@('MsiProvider'); 'msi-claw-cg3em-ap'=@('MsiApProvider');
    'valve-deck-firmware'=@('DeckProvider'); 'oxp3-wmi-ec'=@('Oxp3WmiProvider'); 'oxp-superx-dual-ec'=@('SuperXChargeProvider');
    'oxp-x1-amd-ec'=@('EcProvider',$true,0x04a3); 'oxp-x1-intel-ec'=@('EcProvider',$true,0x04a3);
    'oxp-x2-mini-pro-direct-ec'=@('EcProvider',$true,0x04e5); 'gpd-win-mini-2025'=@('EcProvider',$false,0x1d);
    'ayaneo-cec-bypass'=@('EcProvider',$false,0x1e,0x55,0xaa); 'ayaneo-ceii-bypass'=@('EcProvider',$true,0xd1d1,1,0x65)
}
$bindingRows=foreach($group in ($routes | Group-Object route | Sort-Object Name)) {
    $name=$group.Name
    if(!$expected.Contains($name)){throw "No reviewed binding expectation for $name"}
    $provider,$direct,$register,$pause,$resume=$expected[$name]
    $actual=[BatteryProviderBindingAudit.Probe]::Resolve($name)
    $pass=$actual.Provider -ceq $provider -and $actual.Direct -eq $direct -and $actual.Register -eq $register -and
        ($null -eq $pause -or ($actual.Pause -eq $pause -and $actual.Resume -eq $resume))
    [ordered]@{route=$name;identityRules=$group.Count;deviceClasses=@($group.Group.identity.deviceClass);actual=$actual;match=$pass}
}
if($bindingRows.Count -ne $expected.Count){throw 'A reviewed binding was removed; update the audit only after review.'}
$deck=@($routes | Where-Object route -CEQ 'valve-deck-firmware')
if($deck.Count -ne 1 -or $deck[0].firmwareGate.kind -cne 'exact-board-firmware'){throw 'Expected a single Deck pair gate.'}
$pairs=@($deck[0].firmwareGate.pairs)
if(!$pairs.Count){throw 'Empty Deck firmware pair list'}
$pairKeys=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach($pair in $pairs){
    if(($pair.boardId -isnot [long] -and $pair.boardId -isnot [int]) -or $pair.boardId -lt 0 -or $pair.boardId -gt 255 -or
        ($pair.firmware -isnot [long] -and $pair.firmware -isnot [int]) -or $pair.firmware -lt 0 -or $pair.firmware -gt 65535 -or
        !$pairKeys.Add("$($pair.boardId):$($pair.firmware)")){throw 'Invalid or duplicate Deck firmware pair'}
}
function Get-AxisPartition([int[]]$Literals,[int]$Max){
    $explicit=@($Literals|Sort-Object -Unique)
    $outside=$null
    for($i=0;$i -le $Max;$i++){if($i -notin $explicit){$outside=$i;break}}
    $probes=@(@(0,$Max)+$explicit+@($outside|Where-Object {$null -ne $_})|Sort-Object -Unique)
    [ordered]@{explicit=$explicit;outside=$outside;probes=$probes}
}
$boards=Get-AxisPartition (@($pairs.boardId)+$consumerBoards) 255
$firmwares=Get-AxisPartition (@($pairs.firmware)+$consumerFirmware) 65535
$deckCases=foreach($board in $boards.probes) {
    foreach($firmware in $firmwares.probes) {
        $declared=$pairKeys.Contains("${board}:$firmware")
        $actual=[BatteryProviderBindingAudit.Probe]::DeckAllows($board,$firmware)
        [ordered]@{board=$board;firmware=$firmware;declared=$declared;actual=$actual;match=$declared -eq $actual}
    }
}
$unknown=[BatteryProviderBindingAudit.Probe]::Resolve('__unlisted-audit-route__')
$result=[ordered]@{schemaVersion=1;scope='Exact projected C# selector and Deck gate, inert construction/bytes. Unknown route is unreachable through present audited table.';
    providersSha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($providersBytes)).ToLowerInvariant();
    routesSha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($routeBytes)).ToLowerInvariant();
    toolSha256=(Get-FileHash -LiteralPath $PSCommandPath).Hash.ToLowerInvariant();
    identityRules=$routes.Count;routeBindings=$bindingRows;deckBoardFirmwareCases=$deckCases;
    deckGatePartition=[ordered]@{widths=@{boardBits=8;firmwareBits=16};consumerBoards=$consumerBoards;consumerFirmware=$consumerFirmware;board=$boards;firmware=$firmwares};
    unlistedRouteNegativeControl=$unknown}
if($Out){$result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $Out -Encoding utf8}
$badBindings=@($bindingRows | Where-Object {!$_.match}).Count
$badPairs=@($deckCases | Where-Object {!$_.match}).Count
Write-Output "Provider binding: $($bindingRows.Count) routes, $badBindings mismatches. Deck pairs: $($deckCases.Count) comparisons, $badPairs mismatches. Unlisted factory input: $($unknown.Provider)."
if($badBindings -or $badPairs){exit 1}
