[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ProjectRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$WorkspaceRoot,

    [Parameter(Mandatory = $true)]
    [ValidateSet(
        'test:updater',
        'test:updater-manager-resume',
        'test:updater-resume',
        'test:updater-retry',
        'test:updater-rollback',
        'test:fan-host-paths',
        'test:fan-payload',
        'test:updater-install-chain'
    )]
    [string]$Test,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$PnpmCommand
)

$ErrorActionPreference = 'Stop'
# Preserve native failures as their original exit codes on PowerShell 7 as well.
$PSNativeCommandUseErrorActionPreference = $false
$stopwatch = [Diagnostics.Stopwatch]::StartNew()
$exitCode = $null
$stage = $null
$locationPushed = $false
$workerError = $null

try {
    $absRoot = [IO.Path]::GetFullPath($ProjectRoot)
    $absWorkspace = [IO.Path]::GetFullPath($WorkspaceRoot)
    $stages = @{
        'test:updater' = 'HostedUpdater'
        'test:updater-manager-resume' = 'HostedUpdaterManagerResume'
        'test:updater-resume' = 'HostedUpdaterResume'
        'test:updater-retry' = 'HostedUpdaterRetry'
        'test:updater-rollback' = 'HostedUpdaterRollback'
        'test:fan-host-paths' = 'HostedFanHostPaths'
        'test:fan-payload' = 'HostedFanPayload'
        'test:updater-install-chain' = 'HostedUpdaterInstallChain'
    }
    $stage = $stages[$Test]

    . (Join-Path $PSScriptRoot 'release-full-budget.ps1')
    Assert-YmccFullReleaseFixtureWorker -ProjectRoot $absRoot -WorkspaceRoot $absWorkspace -Stage $stage

    # IsPathRooted alone also accepts drive-relative and root-relative Windows paths.
    if (-not [IO.Path]::IsPathRooted($PnpmCommand) -or
        $PnpmCommand -notmatch '^(?:[A-Za-z]:[\\/]|[\\/]{2}[^\\/]+[\\/][^\\/]+(?:[\\/]|$))' -or
        [IO.Path]::GetFileName($PnpmCommand) -ine 'pnpm.cmd') {
        throw 'FULL_RELEASE_HOSTED_PNPM_PATH_INVALID'
    }
    $pnpmFile = Get-Item -LiteralPath $PnpmCommand -Force -ErrorAction Stop
    if ($pnpmFile -isnot [IO.FileInfo] -or
        $pnpmFile.Name -ine 'pnpm.cmd' -or
        ($pnpmFile.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'FULL_RELEASE_HOSTED_PNPM_FILE_INVALID'
    }

    $package = Get-Content -LiteralPath (Join-Path $absRoot 'package.json') -Raw -Encoding UTF8 -ErrorAction Stop |
        ConvertFrom-Json -ErrorAction Stop
    if ($null -eq $package -or $package.scripts -isnot [pscustomobject]) {
        throw 'FULL_RELEASE_HOSTED_PACKAGE_SCRIPTS_INVALID'
    }
    $script = $package.scripts.PSObject.Properties[$Test]
    if ($null -eq $script -or $script.Name -cne $Test -or
        $script.Value -isnot [string] -or [string]::IsNullOrWhiteSpace($script.Value)) {
        throw 'FULL_RELEASE_HOSTED_TEST_SCRIPT_MISSING'
    }

    Push-Location -LiteralPath $absRoot -ErrorAction Stop
    $locationPushed = $true
    & $PnpmCommand run $Test
    $exitCode = $LASTEXITCODE
    if ($null -eq $exitCode) {
        throw 'FULL_RELEASE_HOSTED_EXIT_CODE_MISSING'
    }
}
catch {
    $workerError = $_
    throw
}
finally {
    try {
        if ($locationPushed) {
            Pop-Location -ErrorAction Stop
        }
    }
    catch {
        # Cleanup must not replace the original exception or a native failure.
        if ($null -ne $workerError -or ($null -ne $exitCode -and $exitCode -ne 0)) {
            Write-Warning ('FULL_RELEASE_HOSTED_LOCATION_RESTORE_FAILED: ' + $_.Exception.Message) -WarningAction Continue
        }
        else {
            throw
        }
    }
    finally {
        $stopwatch.Stop()
        # This is step telemetry, never a session, acceptance receipt, or PASS claim.
        [ordered]@{
            test = $Test
            stage = $stage
            exitCode = $exitCode
            wallMs = $stopwatch.Elapsed.TotalMilliseconds
        } | ConvertTo-Json -Compress | Write-Output
    }
}

exit $exitCode
