param(
    [string]$HostRoot
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $repoRoot)
if ([string]::IsNullOrWhiteSpace($HostRoot)) {
    $HostRoot = Join-Path $workspaceRoot 'FanLab\real-host\bin\Release\net10.0-windows10.0.19041.0\win-x64'
}
$hostRoot = $HostRoot
$hostExe = Join-Path $hostRoot 'YeManFanHost.exe'
$portProbe = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$portProbe.Start()
$port = ([System.Net.IPEndPoint]$portProbe.LocalEndpoint).Port
$portProbe.Stop()
$base = "http://127.0.0.1:$port"
$sessionPath = Join-Path ([System.IO.Path]::GetTempPath()) ("YeManFanHost-parent-watchdog-{0}.session" -f [guid]::NewGuid().ToString('N'))
$session = -join (1..32 | ForEach-Object { '{0:x2}' -f (Get-Random -Minimum 0 -Maximum 256) })
$parent = $null
$hostProcess = $null
$duplicateProcess = $null

try {
    if (-not (Test-Path -LiteralPath $hostExe -PathType Leaf)) { throw "current release Host executable missing: $hostExe" }
    $session | Set-Content -LiteralPath $sessionPath -Encoding ASCII -NoNewline
    $parent = Start-Process -FilePath (Get-Command powershell.exe).Source -ArgumentList @(
        '-NoLogo', '-NoProfile', '-Command', 'Start-Sleep -Seconds 10'
    ) -PassThru -WindowStyle Hidden
    $hostProcess = Start-Process -FilePath $hostExe -ArgumentList @(
        '--port', [string]$port,
        '--protocol-version', '2',
        '--parent-pid', [string]$parent.Id,
        '--session-token-file', $sessionPath
    ) -PassThru -WindowStyle Hidden

    $health = $null
    # Match the product launcher readiness budget (5s) with a small margin;
    # cold .NET startup can be slower under Defender/first-run compilation.
    for ($attempt = 0; $attempt -lt 60; $attempt += 1) {
        try {
            $health = Invoke-RestMethod -Uri "$base/health" -Headers @{ 'X-YeMan-Fan-Session' = $session } -TimeoutSec 1
            break
        } catch { Start-Sleep -Milliseconds 100 }
    }
    if ($null -eq $health -or $health.host -ne 'YeManFanHost') { throw 'safe Host did not expose authenticated health' }
    if ($health.state.hardwareCapable -ne $false -or $health.state.hardwareWritesEnabled -ne $false -or $health.state.hardwareWritesObserved -ne $false) {
        throw 'parent watchdog test Host was not in safe no-hardware mode'
    }

    # The listener port alone is not an ownership lock: the resident Host can
    # recreate it while retaining an HC recovery session. A second safe Host
    # on a different port must still be rejected by the lifetime mutex.
    $duplicateProcess = Start-Process -FilePath $hostExe -ArgumentList @(
        '--port', [string]($port + 1),
        '--protocol-version', '2',
        '--session-token', $session
    ) -PassThru -WindowStyle Hidden
    if (-not $duplicateProcess.WaitForExit(3000) -or $duplicateProcess.ExitCode -ne 2) {
        throw 'second Host was not rejected by the process-lifetime mutex'
    }

    # The parent-exit handoff must answer without waiting for OEM recovery.
    # In the safe Host there is no hardware call, so it then settles to
    # Stopped; a real Host instead remains resident and retries on failure.
    $handoff = Invoke-RestMethod -Uri "$base/api/parent-exit" -Method Post -Headers @{ 'X-YeMan-Fan-Session' = $session } -Body '{}' -ContentType 'application/json' -TimeoutSec 1
    if ($handoff.recoveryAccepted -ne $true -or $handoff.hostWillRemainResident -ne $true) {
        throw 'parent-exit handoff was not accepted by the Host'
    }
    $settled = $false
    for ($attempt = 0; $attempt -lt 20; $attempt += 1) {
        try {
            $state = (Invoke-RestMethod -Uri "$base/api/state" -Headers @{ 'X-YeMan-Fan-Session' = $session } -TimeoutSec 1).state
            if ($state.state -eq 'Stopped' -and $state.hardwareWritesEnabled -eq $false -and $state.hardwareWritesObserved -eq $false) {
                $settled = $true
                break
            }
        } catch { }
        Start-Sleep -Milliseconds 50
    }
    if (-not $settled) { throw 'safe Host parent-exit handoff did not settle without hardware writes' }

    $parent.WaitForExit()
    $deadline = [DateTime]::UtcNow.AddSeconds(8)
    while (-not $hostProcess.HasExited -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 100
        $hostProcess.Refresh()
    }
    if (-not $hostProcess.HasExited) { throw 'Host remained resident after its parent exited' }
    Write-Output 'fan Host parent watchdog selftest: PASS (parent-exit handoff -> safe Host close -> no residual; hardwareWrites=false)'
}
finally {
    if ($null -ne $duplicateProcess -and -not $duplicateProcess.HasExited) { Stop-Process -Id $duplicateProcess.Id -Force -ErrorAction SilentlyContinue }
    if ($null -ne $hostProcess -and -not $hostProcess.HasExited) { Stop-Process -Id $hostProcess.Id -Force -ErrorAction SilentlyContinue }
    if ($null -ne $parent -and -not $parent.HasExited) { Stop-Process -Id $parent.Id -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $sessionPath -PathType Leaf) { Remove-Item -LiteralPath $sessionPath -Force }
}
