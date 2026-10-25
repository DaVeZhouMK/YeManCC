[CmdletBinding()]
param(
    [string]$OutputRoot,
    [string]$VcVarsPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Build artifacts only. The binary uses memory for every fake journal/effect.
# Never includes main.cpp or links/runs product, vendor SDK, service/device APIs.
$repo = Split-Path -Parent $PSScriptRoot
$header = Join-Path $repo 'native\msi_crash_recovery_journal.h'
$test = Join-Path $repo 'tools\msi_crash_recovery_journal_selftest.cpp'
$jsonHeader = @((Join-Path $repo 'deps\json\json.hpp'), (Join-Path $repo 'vendor\nlohmann\json.hpp')) |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $jsonHeader) { throw 'Bundled json.hpp not found' }
$jsonInclude = Split-Path -Parent $jsonHeader
$inputs = @($header, $test, $PSCommandPath, $jsonHeader)
$before = @($inputs | ForEach-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash })
if ([string]::IsNullOrEmpty($OutputRoot)) {
    $OutputRoot = Join-Path ([IO.Path]::GetTempPath()) ('ymcc-msi-crash-recovery-journal-' + [guid]::NewGuid().ToString('N'))
}
$output = [IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $output -Force | Out-Null

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    if (-not (Test-Path -LiteralPath $VcVarsPath -PathType Leaf)) { throw "MSVC environment not found: $VcVarsPath" }
    # cmd is used only to import compiler environment; no filesystem actions.
    $environmentLines = & $env:ComSpec /d /s /c "`"$VcVarsPath`" >nul && set"
    if ($LASTEXITCODE -ne 0) { throw 'vcvars64 failed' }
    foreach ($line in $environmentLines) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }
}
$compiler = (Get-Command cl.exe -ErrorAction Stop).Source
$profiles = @(
    @{ name = 'pure-release'; flags = @('/O2', '/DNDEBUG') },
    @{ name = 'pure-checked'; flags = @('/Od', '/RTC1') }
)
$summary = [Collections.Generic.List[string]]::new()
$summary.Add('scope=pure header + memory fake binary; hardwareAccess=false productRun=false SDK=false persistentIO=false')
$summary.Add("compiler=$compiler")
Push-Location -LiteralPath $output
try {
    foreach ($profile in $profiles) {
        $name = $profile.name
        $exe = Join-Path $output "$name.exe"
        $obj = Join-Path $output "$name.obj"
        $buildLog = Join-Path $output "$name-build.log"
        $runLog = Join-Path $output "$name-run.log"
        $flags = $profile.flags
        & $compiler /nologo /std:c++20 /Zc:__cplusplus /permissive- /W4 /WX /EHsc /utf-8 /MT @flags $test "/I$jsonInclude" "/Fo$obj" "/Fe$exe" "/Fd$output\$name.pdb" /link /INCREMENTAL:NO *> $buildLog
        if ($LASTEXITCODE -ne 0) {
            Get-Content -LiteralPath $buildLog | Select-Object -Last 40
            throw "Pure journal fixture compilation failed: $name"
        }
        & $exe *> $runLog
        if ($LASTEXITCODE -ne 0) {
            Get-Content -LiteralPath $runLog
            throw "Pure journal fixture failed: $name"
        }
        $receipt = [IO.File]::ReadAllText($runLog)
        $match = [regex]::Match($receipt, '(?m)^PURE_FAKE_SUMMARY passed=(\d+) total=(\d+) assertions=(\d+) hardwareAccess=false productRun=false SDK=false persistentIO=false\r?$')
        if (-not $match.Success -or $match.Groups[1].Value -ne $match.Groups[2].Value -or [int]$match.Groups[1].Value -eq 0 -or $receipt -match '(?m)^FAIL ') {
            throw "Invalid pure fake receipt: $name"
        }
        $summary.Add("profile=$name flags=$($flags -join ' ') exitCode=0 $($match.Value.Trim())")
        $summary.Add("binarySHA256=$((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash) path=$exe")
        Get-Content -LiteralPath $runLog
    }
} finally { Pop-Location }
$after = @($inputs | ForEach-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash })
for ($i = 0; $i -lt $inputs.Count; ++$i) {
    if ($before[$i] -cne $after[$i]) { throw "Source changed during verification: $($inputs[$i])" }
    $summary.Add("sourceSHA256=$($after[$i]) path=$($inputs[$i])")
}
$summaryPath = Join-Path $output 'verification-summary.txt'
[IO.File]::WriteAllLines($summaryPath, $summary, [Text.UTF8Encoding]::new($false))
Write-Output "MSI_CRASH_RECOVERY_JOURNAL_PURE_PASSED $summaryPath"
