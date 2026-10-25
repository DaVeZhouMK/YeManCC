#requires -Version 7.0
[CmdletBinding()]
param([string]$OutputRoot = 'G:\YeManCC-Work\Build\Validation\ROG-SD-Modifier-Evidence-R2-20261013\recorder')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$allowed = 'G:\YeManCC-Work\Build\Validation\ROG-SD-Modifier-Evidence-R2-20261013\recorder'
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\', '/')
if (-not ($OutputRoot.Equals($allowed, [StringComparison]::OrdinalIgnoreCase) -or
    $OutputRoot.StartsWith($allowed + '\', [StringComparison]::OrdinalIgnoreCase))) {
    throw 'Recorder artifacts must remain in the authorized R2 recorder directory.'
}
if ($OutputRoot -match '["%!\r\n&|<>^]') { throw 'Shell metacharacters are not allowed in the private output path.' }
$ancestor = $OutputRoot
while ($ancestor) {
    if ((Test-Path -LiteralPath $ancestor) -and
        ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Reparse point in private output ancestry: $ancestor"
    }
    $ancestor = Split-Path -Parent $ancestor
}
$repo = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$runRoot = Join-Path $OutputRoot ('run-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N'))
$snapshot = Join-Path $runRoot 'snapshot'
New-Item -ItemType Directory -Path $snapshot,(Join-Path $runRoot 'temp') -Force | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
function Write-Utf8([string]$Path, [string]$Value) {
    if (-not [IO.Path]::GetFullPath($Path).StartsWith($runRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Output escapes private run: $Path"
    }
    [IO.File]::WriteAllText($Path, $Value, $utf8)
}
function Write-Json([string]$Path, $Value) { Write-Utf8 $Path (ConvertTo-Json -InputObject $Value -Depth 12) }
$processRuns = [Collections.Generic.List[object]]::new()
function Run-PrivateChild([string]$Exe, [string[]]$Arguments, [string]$Name) {
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $Exe
    foreach ($argument in $Arguments) { $psi.ArgumentList.Add($argument) }
    $psi.WorkingDirectory = $runRoot
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.Environment['TEMP'] = Join-Path $runRoot 'temp'
    $psi.Environment['TMP'] = Join-Path $runRoot 'temp'
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = [Diagnostics.Process]::Start($psi)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $timedOut = $false
    try {
        if (-not $process.WaitForExit(25000)) {
            $timedOut = $true
            # Only the Process we just created and its own build descendants.
            $process.Kill($true)
            if (-not $process.WaitForExit(1000)) { throw 'Own child did not terminate within one second.' }
        }
        $timer.Stop()
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        Write-Utf8 (Join-Path $runRoot ($Name + '.stdout.txt')) $stdout
        Write-Utf8 (Join-Path $runRoot ($Name + '.stderr.txt')) $stderr
        $processRuns.Add([pscustomobject]@{
            name = $Name; pid = $process.Id; executable = $Exe; arguments = $Arguments
            createNoWindow = $true; useShellExecute = $false; lifetimeMs = $timer.ElapsedMilliseconds
            exitCode = $process.ExitCode; timedOut = $timedOut
        })
        if ($timedOut) { throw "Own child exceeded its 25-second limit: $Name" }
        return [pscustomobject]@{ exitCode = $process.ExitCode; stdout = $stdout; stderr = $stderr }
    }
    finally { $process.Dispose() }
}
$sourceBinding = [Collections.Generic.List[object]]::new()
$failure = $null
$result = $null
$passed = $false
$binary = Join-Path $runRoot 'keyboard_modifier_event_evidence_selftest.exe'
try {
    foreach ($relative in @('native\keyboard_modifier_event_evidence.h',
        'tools\keyboard_modifier_event_evidence_selftest.cpp', 'tools\verify-keyboard-modifier-event-evidence.ps1')) {
        $source = Join-Path $repo $relative
        $copy = Join-Path $snapshot ([IO.Path]::GetFileName($source))
        $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        Copy-Item -LiteralPath $source -Destination $copy
        if ((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ne $hash) { throw "Source changed during snapshot: $source" }
        $sourceBinding.Add([pscustomobject]@{ source = $source; snapshot = $copy; sha256 = $hash })
    }
    Write-Json (Join-Path $runRoot 'source-binding.json') $sourceBinding.ToArray()
    $header = Get-Content -LiteralPath (Join-Path $snapshot 'keyboard_modifier_event_evidence.h') -Raw
    if ($header -match '\b(SendInput|GetAsyncKeyState|GetKeyboardState|SetWindowsHookExW|GetRawInputData|GetTickCount64|CreateFileW|WriteFile|RegOpenKeyExW|CreateServiceW)\s*\(') {
        throw 'Memory-only helper contains a prohibited live API call.'
    }
    if ($header -match '#include\s*[<"]json' -or $header -notmatch 'capacity\s*=\s*64') {
        throw 'Helper capacity or JSON exclusion audit failed.'
    }
    $writerStart = $header.IndexOf('void record(')
    $writerEnd = $header.IndexOf('Snapshot take(', $writerStart)
    $writer = $header.Substring($writerStart, $writerEnd - $writerStart)
    $appendStart = $header.IndexOf('void appendAtEpoch(')
    $appendEnd = $header.IndexOf('public:', $appendStart)
    $append = $header.Substring($appendStart, $appendEnd - $appendStart)
    if ($writer -notmatch 'capturedEpoch' -or $writer -notmatch 'appendAtEpoch\(copy, capturedEpoch\)' -or
        $append -notmatch 'std::try_to_lock' -or $append -notmatch '!= capturedEpoch' -or
        ($writer + $append) -match 'lock_guard|\.lock\(|\bnew\b|malloc\(|sleep|yield') {
        throw 'Writer must capture/check its epoch and use one nonblocking try-lock without allocation/wait.'
    }
    $vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path -LiteralPath $vcvars)) { throw 'MSVC x64 toolchain unavailable.' }
    $object = Join-Path $runRoot 'keyboard_modifier_event_evidence_selftest.obj'
    $batchPath = Join-Path $runRoot 'build.cmd'
    $batchLines = @('@echo off', ('call "' + $vcvars + '" >nul'), 'if errorlevel 1 exit /b %errorlevel%',
        ('cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 /WX /I"' + $snapshot + '" "' + $snapshot + '\keyboard_modifier_event_evidence_selftest.cpp" /Fo"' + $object + '" /Fe"' + $binary + '" /link /INCREMENTAL:NO'),
        'exit /b %errorlevel%')
    Write-Utf8 $batchPath (($batchLines -join "`r`n") + "`r`n")
    Write-Json (Join-Path $runRoot 'build-inputs.json') @{
        compilerEnvironment = $vcvars; compilerEnvironmentSha256 = (Get-FileHash -LiteralPath $vcvars -Algorithm SHA256).Hash
        command = $batchPath; helper = $sourceBinding[0]; fixture = $sourceBinding[1]
        productionMainCompiled = $false; inputApisExecuted = $false
    }
    $build = Run-PrivateChild (Join-Path $env:SystemRoot 'System32\cmd.exe') @('/d','/c',$batchPath) 'build'
    if ($build.exitCode -ne 0) { throw "Standalone recorder build failed: $($build.stdout) $($build.stderr)" }
    $run = Run-PrivateChild $binary @() 'memory-selftest'
    if ($run.exitCode -ne 0) { throw "Memory selftest failed: $($run.stdout) $($run.stderr)" }
    $result = $run.stdout.Trim() | ConvertFrom-Json
    Write-Json (Join-Path $runRoot 'selftest-result.json') $result
    if (-not $result.passed -or $result.failed -ne 0 -or $result.capacity -ne 64 -or
        $result.heapAllocationsDuringApiChecks -ne 0) { throw 'Selftest result failed required assertions.' }
    $passed = $true
}
catch { $failure = $_.Exception.Message; Write-Host $failure }
finally {
    $stable = $true
    $sourceAfter = foreach ($item in $sourceBinding) {
        $current = (Get-FileHash -LiteralPath $item.source -Algorithm SHA256).Hash
        $snapshotHash = (Get-FileHash -LiteralPath $item.snapshot -Algorithm SHA256).Hash
        if ($current -ne $item.sha256 -or $snapshotHash -ne $item.sha256) { $stable = $false }
        [pscustomobject]@{ source = $item.source; initialSha256 = $item.sha256; currentSha256 = $current; snapshotSha256 = $snapshotHash }
    }
    $passed = $passed -and $stable
    Write-Json (Join-Path $runRoot 'source-hashes-after.json') @($sourceAfter)
    Write-Json (Join-Path $runRoot 'process-runs.json') $processRuns.ToArray()
    $summary = [ordered]@{
        passed = $passed; failure = $failure; sourceStable = $stable
        result = $result; sourceBinding = $sourceBinding.ToArray(); processRuns = $processRuns.ToArray()
        binary = $binary; outputRoot = $runRoot
        productionOrProbeExecuted = $false; inputDeviceServiceOperationsExecuted = $false
        scope = 'Actual recorder/helper production APIs under synthetic data, overflow, contention and cross-thread memory tests. No real input, hook, device, product, probe or service execution; not source attribution or consumer receipt proof.'
        insertedMasksMeaning = 'Caller-provided countBasedPrefixAssumption; not a per-event consumer receipt. win32ErrorHint is not UIPI attribution.'
        modifiedFiles = @('native/keyboard_modifier_event_evidence.h','tools/keyboard_modifier_event_evidence_selftest.cpp','tools/verify-keyboard-modifier-event-evidence.ps1')
    }
    Write-Json (Join-Path $runRoot 'verification-summary.json') $summary
    $artifacts = foreach ($file in Get-ChildItem -LiteralPath $runRoot -Recurse -File | Where-Object { $_.Name -ne 'artifact-binding.json' }) {
        [pscustomobject]@{ path = $file.FullName; bytes = $file.Length; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash }
    }
    Write-Json (Join-Path $runRoot 'artifact-binding.json') @($artifacts)
    Write-Host (ConvertTo-Json -InputObject @{ passed = $passed; failure = $failure; sourceStable = $stable; summary = (Join-Path $runRoot 'verification-summary.json'); result = $result } -Depth 5)
}
if (-not $passed) { exit 1 }
