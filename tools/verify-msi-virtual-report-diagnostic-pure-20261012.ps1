[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This script builds and runs ONLY the standalone pure fake fixture, never the
# product, InputHost, any SDK probe, hardware/service/device or OEM action.
$repo = Split-Path -Parent $PSScriptRoot
$out = [IO.Path]::GetFullPath('G:\YeManCC-Work\_scratch\msi-virtual-report-pair-20261012-1439\pure-agent')
$header = Join-Path $repo 'native\msi_virtual_report_diagnostic.h'
$test = Join-Path $repo 'tools\msi_virtual_report_diagnostic_selftest.cpp'
$inputs = @($header, $test, $PSCommandPath)
$before = @($inputs | ForEach-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash })
New-Item -ItemType Directory -Path $out -Force | Out-Null

# Header dependency allowlist, plus self-contained include-first fixture.
$headerText = [IO.File]::ReadAllText($header)
$allowed = @('cstdint', 'limits', 'optional', 'string', 'utility')
$includes = [regex]::Matches($headerText, '(?m)^\s*#\s*include\s*[<"]([^>"]+)[>"]')
foreach ($match in $includes) {
    if ($match.Groups[1].Value -notin $allowed) { throw "Non-pure header include: $($match.Value)" }
}
if ($includes.Count -ne $allowed.Count) { throw 'Unexpected header dependency count' }
if ($headerText -match '(?i)nlohmann|#\s*include[^\r\n]*(windows|json|fstream|thread|mutex)') {
    throw 'Forbidden pure-header dependency'
}
$testText = [IO.File]::ReadAllText($test)
if ($testText -notmatch '\A//[^\r\n]*\r?\n#include "msi_virtual_report_diagnostic.h"') {
    throw 'Fixture must include the actual standalone header first'
}

$vc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vc -PathType Leaf)) { throw 'MSVC vcvars64 not found' }
# cmd is used ONLY to import the compiler environment, never for file actions.
$environmentLines = & $env:ComSpec /d /s /c "`"$vc`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'vcvars64 failed' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
$compiler = (Get-Command cl.exe -ErrorAction Stop).Source
$profiles = @(
    @{ name = 'pure-release'; flags = @('/O2') },
    @{ name = 'pure-checked'; flags = @('/Od', '/RTC1') }
)
$summary = [Collections.Generic.List[string]]::new()
$summary.Add('scope=pure C++20 helper + fake fixture only; hardwareAccess=false; productRun=false; SDK=false')
$summary.Add("compiler=$compiler")
$summary.Add('commonFlags=/std:c++20 /Zc:__cplusplus /permissive- /W4 /WX /EHsc /utf-8 /MT /showIncludes')
Push-Location -LiteralPath $out
try {
    foreach ($profile in $profiles) {
        $name = $profile.name
        $exe = Join-Path $out "$name.exe"
        $obj = Join-Path $out "$name.obj"
        $buildLog = Join-Path $out "$name-build.log"
        $runLog = Join-Path $out "$name-run.log"
        $flags = $profile.flags
        & $compiler /nologo /std:c++20 /Zc:__cplusplus /permissive- /W4 /WX /EHsc /utf-8 /MT /showIncludes @flags $test "/I$repo\native" "/Fo$obj" "/Fe$exe" "/Fd$out\$name.pdb" /link /INCREMENTAL:NO *> $buildLog
        if ($LASTEXITCODE -ne 0) {
            Get-Content -LiteralPath $buildLog | Select-Object -Last 30
            throw "Pure fixture compilation failed: $name"
        }
        & $exe *> $runLog
        if ($LASTEXITCODE -ne 0) {
            Get-Content -LiteralPath $runLog
            throw "Pure fake selftest failed: $name"
        }
        $receipt = [IO.File]::ReadAllText($runLog)
        if ($receipt -match '(?m)^FAIL ' -or $receipt -notmatch 'PURE_FAKE_SUMMARY passed=15 total=15 assertions=\d+ hardwareAccess=false productRun=false SDK=false') {
            throw "Invalid pure-fake receipt: $name"
        }
        $line = ($receipt -split '\r?\n' | Where-Object { $_ -like 'PURE_FAKE_SUMMARY*' }) -join ''
        $summary.Add("profile=$name flags=$($flags -join ' ') exitCode=0 $line")
        $summary.Add("binarySHA256=$((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash) path=$exe")
        Get-Content -LiteralPath $runLog
    }
} finally { Pop-Location }

$after = @($inputs | ForEach-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash })
for ($i = 0; $i -lt $inputs.Count; ++$i) {
    if ($before[$i] -cne $after[$i]) { throw "Owned source changed during validation: $($inputs[$i])" }
    $summary.Add("sourceSHA256=$($after[$i]) path=$($inputs[$i])")
}
$summaryPath = Join-Path $out 'verification-summary.txt'
[IO.File]::WriteAllLines($summaryPath, $summary, [Text.UTF8Encoding]::new($false))
Write-Output "MSI_VIRTUAL_REPORT_DIAGNOSTIC_PURE_PASSED $summaryPath"
