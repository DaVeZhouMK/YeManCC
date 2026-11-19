param([string]$Out)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$policy = Join-Path $repo 'native/BatteryHost/Policy.cs'
$routePath = Join-Path $repo 'native/BatteryHost/battery-routes.json'
$source = Get-Content -LiteralPath $policy -Raw
$predicate = [regex]::Match($source, '(?ms)^    public static bool FirmwareAllowed\(Route route,Identity id\)\r?\n    \{.*?^    \}')
if (!$predicate.Success) { throw 'FirmwareAllowed shape changed; review the audit projection before using it.' }
$normalizer = [regex]::Match($source, '(?m)^    static string Norm\(string value\)=>[^\r\n]+;')
if (!$normalizer.Success) { throw 'Policy normalization helper changed; review the audit projection.' }

# Compile the actual pure predicate only; no BatteryHost reference or device implementation.
$shim = @"
using System;
using System.Linq;
using System.Text.RegularExpressions;
namespace BatteryFirmwareExactnessAudit {
public record Identity(string Bios, int EcMajor, int EcMinor, string SystemProduct);
public record FirmwareGate(string Kind,int MajorMin,int MinorMin,string Bios,int EcMajor,int EcMinor);
public record MatchRule(string[] Values);
public record Route(string RouteId, MatchRule Identity, FirmwareGate FirmwareGate);
public static class EcProvider { public const string X2MiniProRouteId="oxp-x2-mini-pro-direct-ec"; }
public static class Predicate {
$($normalizer.Value)
$($predicate.Value)
}}
"@
Add-Type -TypeDefinition $shim

$all = Get-Content -LiteralPath $routePath -Raw | ConvertFrom-Json -AsHashtable
$cases = foreach ($row in $all | Where-Object { $_.firmwareGate.kind -in 'exact-reviewed-firmware', 'oxp-x2-mini-pro-field-firmware' }) {
    $g = $row.firmwareGate
    $system = $row.identity.values[0]
    $route = [BatteryFirmwareExactnessAudit.Route]::new($row.route,
        [BatteryFirmwareExactnessAudit.MatchRule]::new([string[]]$row.identity.values),
        [BatteryFirmwareExactnessAudit.FirmwareGate]::new($g.kind,$g.majorMin,$g.minorMin,$g.bios,$g.ecMajor,$g.ecMinor))
    $probes = @(
        @('reviewed', $g.bios, $g.ecMajor, $g.ecMinor, $system, $true),
        @('different-ec', $g.bios, $g.ecMajor, ($g.ecMinor + 1), $system, $false),
        @('unknown-ec', $g.bios, 255, 255, $system, $false),
        @('longer-number', "1$($g.bios)", $g.ecMajor, $g.ecMinor, $system, $false),
        @('version-suffix', "$($g.bios).1", $g.ecMajor, $g.ecMinor, $system, $false),
        @('version-prefix', "1.$($g.bios)", $g.ecMajor, $g.ecMinor, $system, $false),
        @('undocumented-vendor-label', "Beta $($g.bios)", $g.ecMajor, $g.ecMinor, $system, $false),
        @('missing-bios', '', $g.ecMajor, $g.ecMinor, $system, $false)
    )
    if ($g.kind -eq 'oxp-x2-mini-pro-field-firmware') {
        $probes += ,@('op3-system-conflict', $g.bios, $g.ecMajor, $g.ecMinor, 'ONEXPLAYER 3', $false)
    }
    foreach ($probe in $probes) {
        $label, $bios, $major, $minor, $product, $expected = $probe
        $identity = [BatteryFirmwareExactnessAudit.Identity]::new($bios,$major,$minor,$product)
        $actual = [BatteryFirmwareExactnessAudit.Predicate]::FirmwareAllowed($route,$identity)
        [ordered]@{ route=$row.route; case=$label; bios=$bios; ecMajor=$major; ecMinor=$minor;
            systemProduct=$product; expected=$expected; actual=$actual; match=($expected -eq $actual) }
    }
}
if (!$cases -or !($cases.route -contains 'gpd-win-mini-2025') -or !($cases.route -contains 'oxp-x2-mini-pro-direct-ec')) {
    throw 'Expected exact firmware gates are unavailable; review the audit scope.'
}
$failures = @($cases | Where-Object { !$_.match })
$result = [ordered]@{
    schemaVersion=1; scope='isolated compiled FirmwareAllowed source with reviewed row metadata; not a provider or live Host test'
    policySha256=(Get-FileHash -LiteralPath $policy).Hash.ToLowerInvariant()
    routesSha256=(Get-FileHash -LiteralPath $routePath).Hash.ToLowerInvariant()
    caseCount=$cases.Count; mismatchCount=$failures.Count; cases=$cases
}
if ($Out) { $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Out -Encoding utf8 }
Write-Output "Exact firmware audit: $($cases.Count) checks, $($failures.Count) mismatches."
foreach ($failure in $failures) { Write-Output "$($failure.route): BIOS '$($failure.bios)' expected $($failure.expected), actual $($failure.actual)" }
if ($failures.Count) { exit 1 }
