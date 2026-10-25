[CmdletBinding()]
param(
    [string]$OutputRoot,
    [string]$VcVarsPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Compile/run only this standalone fixture. No product build, main.cpp,
# SDK, device, registry or service calls. Ledger IO uses private temp paths.
$repo = Split-Path -Parent $PSScriptRoot
$header = Join-Path $repo 'native\external_controller_priority_ledger.h'
$test = Join-Path $repo 'tools\external_controller_priority_ledger_selftest.cpp'
$jsonHeader = @((Join-Path $repo 'deps\json\json.hpp'), (Join-Path $repo 'vendor\nlohmann\json.hpp')) |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $jsonHeader) { throw 'Bundled json.hpp not found' }
$jsonInclude = Split-Path -Parent $jsonHeader
$inputs = @($header, $test, $PSCommandPath, $jsonHeader)
$before = @($inputs | ForEach-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash })
if ([string]::IsNullOrEmpty($OutputRoot)) {
    $OutputRoot = Join-Path ([IO.Path]::GetTempPath()) ('ymcc-externalpad-ledger-build-' + [guid]::NewGuid().ToString('N'))
}
# Even a caller-supplied output parent gets a new isolated child directory.
$outputParent = [IO.Path]::GetFullPath($OutputRoot)
$output = Join-Path $outputParent ('standalone-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output -Force | Out-Null

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    if (-not (Test-Path -LiteralPath $VcVarsPath -PathType Leaf)) { throw "MSVC environment not found: $VcVarsPath" }
    # cmd imports compiler environment only; no filesystem operations.
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
    @{ name = 'standalone-release'; flags = @('/O2', '/DNDEBUG') },
    @{ name = 'standalone-checked'; flags = @('/Od', '/RTC1') }
)
$summary = [Collections.Generic.List[string]]::new()
$summary.Add('scope=standalone header + private-temp filesystem fixture; hardwareAccess=false productRun=false')
$summary.Add("compiler=$compiler")
Push-Location -LiteralPath $output
try {
    # Compile the unmodified header as the first/only include, without fixture API
    # interception. This checks the standalone public interface; no code is run.
    $smoke = Join-Path $output 'standalone-header.cpp'
    $includePath = $header.Replace('\', '/')
    $smokeText = '#include "' + $includePath + '"' + [Environment]::NewLine +
        'static_assert(ymcc::externalpad::ledgerVersion == 1);' + [Environment]::NewLine +
        'bool (*loadPort)(const std::wstring&, ymcc::externalpad::VisibilityDebt&, std::string&) = &ymcc::externalpad::loadDebt;' + [Environment]::NewLine +
        'bool (*savePort)(const std::wstring&, const ymcc::externalpad::VisibilityDebt&, std::string&) = &ymcc::externalpad::saveDebt;' + [Environment]::NewLine
    [IO.File]::WriteAllText($smoke, $smokeText, [Text.UTF8Encoding]::new($false))
    $smokeLog = Join-Path $output 'standalone-header-build.log'
    $smokeObj = Join-Path $output 'standalone-header.obj'
    & $compiler /nologo /std:c++17 /Zc:__cplusplus /permissive- /W4 /WX /EHsc /utf-8 /MT /c $smoke "/I$jsonInclude" "/Fo$smokeObj" *> $smokeLog
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath $smokeLog | Select-Object -Last 40
        throw 'Standalone header interface compilation failed'
    }
    $summary.Add('standaloneHeaderCompile=passed firstInclude=true fixtureInterception=false codeRun=false')
    foreach ($profile in $profiles) {
        $name = $profile.name
        $exe = Join-Path $output "$name.exe"
        $obj = Join-Path $output "$name.obj"
        $buildLog = Join-Path $output "$name-build.log"
        $runLog = Join-Path $output "$name-run.log"
        $flags = $profile.flags
        & $compiler /nologo /std:c++17 /Zc:__cplusplus /permissive- /W4 /WX /EHsc /utf-8 /MT @flags $test "/I$jsonInclude" "/Fo$obj" "/Fe$exe" "/Fd$output\$name.pdb" /link /INCREMENTAL:NO *> $buildLog
        if ($LASTEXITCODE -ne 0) {
            Get-Content -LiteralPath $buildLog | Select-Object -Last 40
            throw "Standalone ledger compilation failed: $name"
        }
        & $exe *> $runLog
        if ($LASTEXITCODE -ne 0) {
            Get-Content -LiteralPath $runLog
            throw "Standalone ledger selftest failed: $name"
        }
        $receipt = [IO.File]::ReadAllText($runLog)
        $match = [regex]::Match($receipt, '(?m)^EXTERNALPAD_LEDGER_SUMMARY passed=(\d+) total=(\d+) assertions=(\d+) hardwareAccess=false productRun=false filesystem=private-temp-only\r?$')
        if (-not $match.Success -or $match.Groups[1].Value -ne $match.Groups[2].Value -or [int]$match.Groups[1].Value -eq 0 -or $receipt -match '(?m)^FAIL ') {
            throw "Invalid standalone ledger receipt: $name"
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
Write-Output "EXTERNALPAD_LEDGER_PASSED $summaryPath"
