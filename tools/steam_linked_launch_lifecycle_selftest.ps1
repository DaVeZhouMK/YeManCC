# Isolated Win32 lifecycle reproduction. NO Steam/addon/YMCC process is launched
# or terminated. VBS/BAT wrappers and two inert workers live in fixtures only.
[CmdletBinding()]
param([string]$OutputDirectory)
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $PSScriptRoot '..\..\..\Build\Validation\steam-linked-launch-20261011' }
$ErrorActionPreference = 'Stop'
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$fixtureRoot = Join-Path $OutputDirectory ('lifecycle fixture ' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
$powerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$cscript = Join-Path $env:SystemRoot 'System32\cscript.exe'
$native = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\native\main.cpp') -Raw
$start = $native.IndexOf('ipc_on("shell.hidden",')
$end = $native.IndexOf("`n    ipc_on(", $start + 1)
$handler = $native.Substring($start, $end - $start)
if ($handler -notmatch 'CREATE_NO_WINDOW' -or $handler -match 'AssignProcessToJobObject|TerminateJobObject|WaitForSingleObject') {
    throw 'Production hidden process contract changed; review fixture before trusting results.'
}
Add-Type -Path (Join-Path $PSScriptRoot 'fixtures\steam_linked_launch_win32_fixture.cs')
$worker = Join-Path $fixtureRoot 'inert-worker.ps1'
@(
    'param([string]$Marker)',
    '$ErrorActionPreference = ''Stop''',
    '$PID | Set-Content -LiteralPath ($Marker + ''.pid'') -Encoding Ascii',
    '$end = [DateTime]::UtcNow.AddSeconds(20)',
    '$stream = [IO.File]::Open(($Marker + ''.ticks''), [IO.FileMode]::Append, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)',
    '$writer = New-Object IO.StreamWriter($stream)',
    'try {',
    '    while ([DateTime]::UtcNow -lt $end) {',
    '        $writer.WriteLine(''alive''); $writer.Flush()',
    '        Start-Sleep -Milliseconds 100',
    '    }',
    '} finally { $writer.Dispose(); $stream.Dispose() }'
) | Set-Content -LiteralPath $worker -Encoding UTF8
$knownMarkers = New-Object 'System.Collections.Generic.List[string]'
function Make-Chain([string]$Name) {
    $directory = Join-Path $fixtureRoot $Name
    New-Item -ItemType Directory -Path $directory | Out-Null
    $markers = @((Join-Path $directory 'steam-fixture'), (Join-Path $directory 'addon-fixture'))
    foreach ($marker in $markers) { $knownMarkers.Add($marker) }
    $inner = Join-Path $directory 'inner.bat'; $outer = Join-Path $directory 'outer.bat'; $vbs = Join-Path $directory 'launch.vbs'
    $innerLines = @('@echo off')
    foreach ($marker in $markers) { $innerLines += ('start "" /b "' + $powerShell + '" -NoProfile -ExecutionPolicy Bypass -File "' + $worker + '" -Marker "' + $marker + '" >"' + $marker + '.stdout" 2>"' + $marker + '.stderr"') }
    $innerLines += 'exit /b 0'
    $innerLines | Set-Content -LiteralPath $inner -Encoding Ascii
    # Disposable asynchronous wrapper stands in for NirCmd's exec hide.
    @('@echo off', ('start "" /b "' + $env:ComSpec + '" /d /c "' + $inner + '"'), 'exit /b 0') | Set-Content -LiteralPath $outer -Encoding Ascii
    @(
        'Set ws = CreateObject("Wscript.Shell")',
        ('ws.Run "cmd /d /c ""' + $outer + '""", 0, True'),
        'Set fso = CreateObject("Scripting.FileSystemObject")',
        'For i = 1 To 100',
        ('  If fso.FileExists("' + $markers[0] + '.ticks") And fso.FileExists("' + $markers[1] + '.ticks") Then Exit For'),
        '  WScript.Sleep 100',
        'Next', 'WScript.Sleep 300'
    ) | Set-Content -LiteralPath $vbs -Encoding Ascii
    return [pscustomobject]@{ Vbs = $vbs; Markers = $markers }
}
function Read-Counts($Markers) {
    return @($Markers | ForEach-Object { $file = $_ + '.ticks'; if (Test-Path -LiteralPath $file) { (Get-Item -LiteralPath $file).Length } else { 0 } })
}
try {
    $sync = Make-Chain 'sync-job'
    [void][SteamLinkedLaunchFixture]::Launch($cscript, $sync.Vbs, $true)
    $first = Read-Counts $sync.Markers
    if (@($first | Where-Object { $_ -lt 1 }).Count) { throw 'Sync fixture workers never became ready; reproduction is inconclusive.' }
    Start-Sleep -Milliseconds 600
    $second = Read-Counts $sync.Markers
    if ($first[0] -ne $second[0] -or $first[1] -ne $second[1]) { throw 'Sync Job did not stop descendants as expected.' }
    Write-Output 'PASS: old synchronous Job reproduces both linked workers being killed on wrapper exit'
    $hidden = Make-Chain 'hidden'
    $wrapperPid = [SteamLinkedLaunchFixture]::Launch($cscript, $hidden.Vbs, $false)
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
        $counts = Read-Counts $hidden.Markers
        if (-not (Get-Process -Id $wrapperPid -ErrorAction SilentlyContinue) -and $counts[0] -gt 0 -and $counts[1] -gt 0) { break }
        Start-Sleep -Milliseconds 100
    }
    if (Get-Process -Id $wrapperPid -ErrorAction SilentlyContinue) { throw 'Hidden wrapper did not exit within the fixture deadline.' }
    $first = Read-Counts $hidden.Markers
    if (@($first | Where-Object { $_ -lt 1 }).Count) { throw 'Hidden fixture workers never became ready.' }
    Start-Sleep -Milliseconds 600
    $second = Read-Counts $hidden.Markers
    if ($second[0] -le $first[0] -or $second[1] -le $first[1]) { throw ('Hidden descendants did not survive wrapper exit: first=' + ($first -join ',') + '; second=' + ($second -join ',')) }
    Write-Output 'PASS: detached hidden launch preserves both workers after all startup wrappers exit'
    @{ passed = $true; cases = 2; oldSyncKillsDescendants = $true; hiddenWorkersSurvive = $true; realSteamOperations = 0; installationWrites = 0; fixtureRoot = $fixtureRoot } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'lifecycle-results.json') -Encoding UTF8
} finally {
    # Only stop exact fixture PIDs after confirming their command line contains
    # this fixture's unique worker and marker paths. Never target a process name.
    foreach ($marker in $knownMarkers) {
        $pidFile = $marker + '.pid'
        if (-not (Test-Path -LiteralPath $pidFile)) { continue }
        $workerId = 0
        if (-not [int]::TryParse((Get-Content -LiteralPath $pidFile -Raw).Trim(), [ref]$workerId)) { continue }
        $process = Get-CimInstance Win32_Process -Filter ("ProcessId=" + $workerId) -ErrorAction SilentlyContinue
        if ($process -and $process.ExecutablePath -ieq $powerShell -and
            $process.CommandLine.IndexOf($worker, [StringComparison]::OrdinalIgnoreCase) -ge 0 -and
            $process.CommandLine.IndexOf($marker, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            Stop-Process -Id $workerId -Force -ErrorAction SilentlyContinue
        }
    }
}
