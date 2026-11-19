param([Parameter(Mandatory)][string]$Scratch)

$ErrorActionPreference = 'Stop'
$scratchPath = [IO.Path]::GetFullPath($Scratch)
if (Test-Path -LiteralPath $scratchPath) { throw 'Expected a new scratch directory' }
New-Item -ItemType Directory -Path $scratchPath | Out-Null
$cases = @(
    @{ name='normalized-only'; expected='indeterminate'; raw=@{} },
    @{ name='exact-raw'; expected='hit'; raw=@{ boardManufacturerRaw='prefix ONE-NETBOOK suffix'; boardProductRaw='ONEXPLAYER SUPER X' } },
    @{ name='raw-product-padding'; expected='miss'; raw=@{ boardManufacturerRaw='prefix ONE-NETBOOK suffix'; boardProductRaw='ONEXPLAYER SUPER X ' } },
    @{ name='raw-manufacturer-case'; expected='miss'; raw=@{ boardManufacturerRaw='prefix One-Netbook suffix'; boardProductRaw='ONEXPLAYER SUPER X' } },
    @{ name='raw-manufacturer-missing'; expected='indeterminate'; raw=@{ boardProductRaw='ONEXPLAYER SUPER X' } },
    @{ name='raw-product-missing'; expected='indeterminate'; raw=@{ boardManufacturerRaw='prefix ONE-NETBOOK suffix' } },
    @{ name='raw-empty-product'; expected='miss'; raw=@{ boardManufacturerRaw='prefix ONE-NETBOOK suffix'; boardProductRaw='' } }
)
$zipPath = Join-Path $scratchPath 'YeManCC-logs-ordinal-fixture.zip'
$zip = [IO.Compression.ZipFile]::Open($zipPath, [IO.Compression.ZipArchiveMode]::Create)
try {
    $writer = [IO.StreamWriter]::new($zip.CreateEntry('battery-charge-host.log').Open())
    try {
        $pidValue = 100
        foreach ($case in $cases) {
            $id = @{ manufacturer='prefix ONE-NETBOOK suffix'; boardProduct='ONEXPLAYER SUPER X'; systemProduct=$case.name }
            foreach ($key in $case.raw.Keys) { $id[$key] = $case.raw[$key] }
            $writer.WriteLine((@{ event='in-process-start'; pid=$pidValue; detail=$id } | ConvertTo-Json -Compress))
            $pidValue++
        }
    } finally { $writer.Dispose() }
} finally { $zip.Dispose() }

$facts = Join-Path $scratchPath 'FACTS.json'
$matrix = Join-Path $scratchPath 'MATRIX.json'
$pwsh = Join-Path $PSHOME 'pwsh.exe'
& $pwsh -NoProfile -File (Join-Path $PSScriptRoot 'battery-field-coverage-audit.ps1') -LogRoot $scratchPath -Out $facts
if ($LASTEXITCODE -ne 0) { throw 'Synthetic field scan failed' }
& node (Join-Path $PSScriptRoot 'battery-field-route-matrix.mjs') $facts $matrix
if ($LASTEXITCODE -ne 0) { throw 'Synthetic matrix failed' }
$report = Get-Content -LiteralPath $matrix -Raw | ConvertFrom-Json -AsHashtable
$rule = 'battery.rule.oxp-superx-dual-ec.OneXPlayerSuperX'
$results = @(foreach ($case in $cases) {
    $profiles = @($report.profiles | Where-Object { $_.identity.systemProduct -ceq $case.name })
    if ($profiles.Count -ne 1) { throw "Missing or repeated profile: $($case.name)" }
    $p = $profiles[0]
    $actual = if ($p.hits -ccontains $rule) {'hit'} elseif ($p.indeterminateOrdinalRuleIds -ccontains $rule) {'indeterminate'} else {'miss'}
    $status = switch ($case.expected) { 'hit' {'unique'} 'miss' {'no-match'} default {'indeterminate'} }
    $expectedRule = if ($status -ceq 'unique') { $rule } else { $null }
    if ($p.selection.status -cne $status -or $p.selection.ruleId -cne $expectedRule) { throw "Wrong static selection: $($case.name)" }
    [ordered]@{ name=$case.name; expected=$case.expected; actual=$actual; selection=$p.selection }
})
$results | ConvertTo-Json -Depth 6
if (@($results | Where-Object { $_.actual -cne $_.expected }).Count) {
    throw 'Ordinal evidence was misclassified'
}
