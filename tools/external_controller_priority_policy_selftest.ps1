[CmdletBinding()]
param(
    [string]$OutputRoot,
    [string]$VcVarsPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Standalone production policy only. No Native/Host, device, driver, or registry calls.
$repo = Split-Path -Parent $PSScriptRoot
$test = Join-Path $PSScriptRoot 'external_controller_priority_policy_selftest.cpp'
$header = Join-Path $repo 'native\external_controller_priority_policy.h'
$sourceHash = (Get-FileHash -LiteralPath $header -Algorithm SHA256).Hash
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path ([IO.Path]::GetTempPath()) ('ymcc-externalpad-policy-' + [guid]::NewGuid().ToString('N'))
}
$output = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) ('standalone-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    if (-not (Test-Path -LiteralPath $VcVarsPath -PathType Leaf)) { throw "MSVC environment missing: $VcVarsPath" }
    $compilerEnvironment = & $env:ComSpec /d /s /c "`"$VcVarsPath`" >nul && set"
    if ($LASTEXITCODE -ne 0) { throw 'vcvars64 failed' }
    foreach ($line in $compilerEnvironment) {
        if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
    }
}
$compiler = (Get-Command cl.exe -ErrorAction Stop).Source
$profiles = @(@{ name = 'release'; flags = @('/O2', '/DNDEBUG') }, @{ name = 'checked'; flags = @('/Od', '/RTC1') })
$receipts = [Collections.Generic.List[string]]::new()
$receipts.Add("productionHeaderSHA256=$sourceHash path=$header")
Push-Location -LiteralPath $output
try {
    foreach ($profile in $profiles) {
        $name = $profile.name; $flags = $profile.flags
        $exe = Join-Path $output "$name.exe"; $buildLog = Join-Path $output "$name-build.log"; $runLog = Join-Path $output "$name-run.log"
        & $compiler /nologo /std:c++17 /Zc:__cplusplus /permissive- /W4 /WX /EHsc /utf-8 /MT @flags $test "/Fo$output\$name.obj" "/Fe$exe" "/Fd$output\$name.pdb" /link /INCREMENTAL:NO *> $buildLog
        if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $buildLog; throw "Policy compilation failed: $name" }
        & $exe *> $runLog
        $testExit = $LASTEXITCODE
        Get-Content -LiteralPath $runLog
        $receipt = [IO.File]::ReadAllText($runLog)
        $match = [regex]::Match($receipt, '(?m)^EXTERNALPAD_POLICY_SUMMARY passed=(\d+) total=(\d+) assertions=(\d+) hardwareAccess=false productRun=false productionHeader=true\r?$')
        if ($testExit -ne 0 -or -not $match.Success -or $match.Groups[1].Value -ne $match.Groups[2].Value -or [int]$match.Groups[2].Value -lt 19) {
            throw "Policy selftest failed: $name (exit $testExit)"
        }
        $receipts.Add("profile=$name $($match.Value.Trim()) binarySHA256=$((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash)")
    }
} finally { Pop-Location }
$afterHash = (Get-FileHash -LiteralPath $header -Algorithm SHA256).Hash
if ($afterHash -cne $sourceHash) { throw 'Production policy changed during verification; rerun against the current source.' }
$receipts.Add("fixtureSHA256=$((Get-FileHash -LiteralPath $test -Algorithm SHA256).Hash)")
$summary = Join-Path $output 'verification-summary.txt'
[IO.File]::WriteAllLines($summary, $receipts, [Text.UTF8Encoding]::new($false))
Write-Output "EXTERNALPAD_POLICY_PASSED $summary"
