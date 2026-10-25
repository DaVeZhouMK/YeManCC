[CmdletBinding()]
param(
    [string]$OutputRoot,
    [ValidateRange(5, 120)][int]$FixtureTimeoutSeconds = 30,
    [string]$VcVarsPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Only the standalone fixture runs. Device/process effects and saves are faked.
# R53 ledger reads use retained files in unique private-temp fixture directories.
$repo = Split-Path -Parent $PSScriptRoot
$test = Join-Path $PSScriptRoot 'external_controller_priority_runtime_selftest.cpp'
$inputs = @($PSCommandPath, $test, (Join-Path $repo 'native\external_controller_priority_runtime.h'),
    (Join-Path $repo 'native\external_controller_priority_policy.h'), (Join-Path $repo 'native\external_controller_priority_ledger.h'))
$jsonHeader = @((Join-Path $repo 'deps\json\json.hpp'), (Join-Path $repo 'vendor\nlohmann\json.hpp')) |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $jsonHeader) { throw 'Bundled json.hpp missing' }
$inputs += $jsonHeader
$hashes = @($inputs | ForEach-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash })
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path ([IO.Path]::GetTempPath()) ('ymcc-externalpad-runtime-' + [guid]::NewGuid().ToString('N'))
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
$receipts = [Collections.Generic.List[string]]::new(); $failed = $false
for ($i = 0; $i -lt $inputs.Count; ++$i) { $receipts.Add("sourceSHA256=$($hashes[$i]) path=$($inputs[$i])") }
Push-Location -LiteralPath $output
try {
    foreach ($profile in $profiles) {
        $name = $profile.name; $flags = $profile.flags
        $exe = Join-Path $output "$name.exe"; $buildLog = Join-Path $output "$name-build.log"; $runLog = Join-Path $output "$name-run.log"
        & $compiler /nologo /std:c++17 /Zc:__cplusplus /permissive- /W4 /WX /EHsc /utf-8 /MT @flags $test "/I$(Split-Path -Parent $jsonHeader)" "/Fo$output\$name.obj" "/Fe$exe" "/Fd$output\$name.pdb" /link /INCREMENTAL:NO *> $buildLog
        if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $buildLog; throw "Runtime fixture compilation failed: $name" }
        # Inspect linked imports before execution: no device, registry, driver, or process-launch imports.
        # CreateFileW/ReadFile are also used by the private-temp R53 ledger fixture; no product paths are opened.
        $imports = & dumpbin.exe /nologo /imports $exe 2>&1
        $imports | Set-Content -LiteralPath (Join-Path $output "$name-imports.log") -Encoding utf8
        if ($LASTEXITCODE -ne 0) { throw "Cannot inspect fixture imports: $name" }
        if (($imports -join "`n") -match '(?im)^\s+(?:[0-9a-f]+\s+)?(?:CreateProcess[AW]|DeviceIoControl|SetupDi\w+|CM_\w+|Reg\w+|XInputGetState|MoveFileEx[AW]|DeleteFile[AW])\s*$') {
            throw "Real external-operation import in standalone fixture: $name"
        }
        # Bound only this import-checked offline fixture, including mutex regressions.
        $start = [Diagnostics.ProcessStartInfo]::new()
        $start.FileName = $exe; $start.WorkingDirectory = $output
        $start.UseShellExecute = $false; $start.CreateNoWindow = $true
        $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
        $process = [Diagnostics.Process]::new(); $process.StartInfo = $start
        try {
            if (-not $process.Start()) { throw "Cannot start standalone fixture: $name" }
            $stdout = $process.StandardOutput.ReadToEndAsync()
            $stderr = $process.StandardError.ReadToEndAsync()
            $exited = $process.WaitForExit($FixtureTimeoutSeconds * 1000)
            if (-not $exited) {
                $process.Kill()
                if (-not $process.WaitForExit(2000)) { throw "Cannot stop timed-out standalone fixture: $name" }
            }
            $testExit = $process.ExitCode
            [IO.File]::WriteAllText($runLog, $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult(), [Text.UTF8Encoding]::new($false))
        } finally { $process.Dispose() }
        Get-Content -LiteralPath $runLog
        if (-not $exited) {
            $failed = $true; $receipts.Add("profile=$name FIXTURE_TIMEOUT seconds=$FixtureTimeoutSeconds exitCode=$testExit")
            continue
        }
        $receipt = [IO.File]::ReadAllText($runLog)
        $match = [regex]::Match($receipt, '(?m)^EXTERNALPAD_RUNTIME_SUMMARY passed=(\d+) total=(\d+) assertions=(\d+) hardwareAccess=false productRun=false productionHeader=true operations=faked-external-effects ledgerReads=private-temp-fixtures\r?$')
        if (-not $match.Success -or [int]$match.Groups[2].Value -lt 33) { throw "Invalid runtime receipt: $name" }
        if ($testExit -ne 0 -or $match.Groups[1].Value -ne $match.Groups[2].Value) { $failed = $true }
        $receipts.Add("profile=$name exitCode=$testExit $($match.Value.Trim()) binarySHA256=$((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash)")
    }
} finally { Pop-Location }
for ($i = 0; $i -lt $inputs.Count; ++$i) {
    $after = (Get-FileHash -LiteralPath $inputs[$i] -Algorithm SHA256).Hash
    if ($after -cne $hashes[$i]) { $failed = $true; $receipts.Add("SOURCE_CHANGED_DURING_RUN path=$($inputs[$i]) currentSHA256=$after") }
}
$summary = Join-Path $output 'verification-summary.txt'
[IO.File]::WriteAllLines($summary, $receipts, [Text.UTF8Encoding]::new($false))
Write-Output "EXTERNALPAD_RUNTIME_RECEIPT $summary"
if ($failed) { throw 'Runtime regressions failed or production changed during verification. Failures remain failures; see the receipt.' }
Write-Output "EXTERNALPAD_RUNTIME_PASSED $summary"
