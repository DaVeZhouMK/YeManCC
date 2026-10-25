[CmdletBinding()]
param([string]$Out)
$ErrorActionPreference = 'Stop'
$shapeProjectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Out)) {
    $shapeWorkspace = [IO.Path]::GetFullPath((Join-Path $shapeProjectRoot '..\..\..'))
    $Out = Join-Path $shapeWorkspace 'Build\Validation\InputHostResultShape'
}
$shapeRunner = Join-Path $PSScriptRoot 'virtual_submit_result_perf_selftest.py'
& python -X utf8 $shapeRunner --shape-only --out $Out
if ($LASTEXITCODE -ne 0) { throw "Actual InputHost result shape selftest failed (exit=$LASTEXITCODE); see $Out" }
[pscustomobject]@{
    ok = $true
    function = 'inputHostSubmitPad'
    verification = 'compiled actual result construction; output/frameProof/rightStick/gyroDps objects'
    deviceOrHostExecuted = $false
    benchmarkExecuted = $false
    output = [IO.Path]::GetFullPath($Out)
} | ConvertTo-Json -Compress
