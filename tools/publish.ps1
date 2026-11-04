<#
.SYNOPSIS
  Compatibility entry point for the rebuilt release chain.

.DESCRIPTION
  This script deliberately does not deploy to C:\SOFT\YeMan. It only builds
  and packages the workspace Release directories. Formal deployment requires a
  separate, explicitly authorized task.
  YMCC standard export and this release entry converge on package-release.ps1
  (package.json: release -> build -> package). HC-SLIM-01 keeps the four large
  runtime DLLs and HIDMaestro; the two duplicate utility placements are omitted
  only while their pinned HC-runtime authorities remain available. No separate
  legacy ZIP or unverified slimming policy is assembled by this wrapper.
#>
[CmdletBinding()]
param(
  [string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT,
  [switch]$DeployInstalled,
  [switch]$TestExport,
  [switch]$FullAcceptance,
  [switch]$FastRelease,
  [string]$ReleaseBugPlan='',
  [ValidateRange(1,1800)][int]$FixtureBudgetSeconds=1800
)

function Invoke-YmccPublishBody {
[CmdletBinding()]
param(
  [string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT,
  [switch]$DeployInstalled,
  [switch]$TestExport,
  [switch]$FullAcceptance,
  [switch]$FastRelease,
  [string]$ReleaseBugPlan='',
  [ValidateRange(1,1800)][int]$FixtureBudgetSeconds=1800
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$previousWorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT
$previousTestExport = $env:YEMAN_TEST_EXPORT
$previousFullAcceptance = $env:YEMAN_FULL_ACCEPTANCE
$previousFixtureSession = $env:YMCC_RELEASE_FIXTURE_SESSION
if($ReleaseBugPlan){$ReleaseBugPlan=(Resolve-Path -LiteralPath $ReleaseBugPlan -ErrorAction Stop).ProviderPath;if(-not(Test-Path -LiteralPath $ReleaseBugPlan -PathType Leaf)){throw 'ReleaseBugPlan must be a file.'}}

Push-Location $ProjectRoot
try {
  if (-not [string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $env:YEMAN_WORKSPACE_ROOT = [IO.Path]::GetFullPath($WorkspaceRoot)
  }
  # Explicit test export is local-only; ordinary export defaults to no fixtures; FullAcceptance is explicit.
  if ($TestExport -and $FullAcceptance) { throw 'Choose ordinary export or explicit FullAcceptance.' }
  if($ReleaseBugPlan -and -not $FastRelease){throw 'ReleaseBugPlan requires explicit FastRelease; full acceptance remains separate.'}
  if ($FastRelease -and ($FullAcceptance -or $TestExport)) { throw 'FastRelease is separate from TestExport and FullAcceptance.' }
  $TestExport = -not [bool]$FullAcceptance
  $env:YEMAN_FULL_ACCEPTANCE = if ($FullAcceptance) { '1' } else { '0' }
  $env:YEMAN_TEST_EXPORT = if ($FullAcceptance) { '0' } else { '1' }
  if ($TestExport -and -not $FastRelease) { Write-Output 'YMCC_TEST_EXPORT: complete local test package; formal release gates are not accepted.' }
  if ($FastRelease) {
    Write-Output 'YMCC_FAST_RELEASE: fixtures NOT_RUN; compilation, locked payload and ZIP identity remain required.'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectRoot 'tools\build-workspace.ps1') -WorkspaceRoot $env:YEMAN_WORKSPACE_ROOT -FastRelease
    if ($LASTEXITCODE -ne 0) { throw "Fast release build failed: exit=$LASTEXITCODE" }
    if($ReleaseBugPlan){
      . (Join-Path $ProjectRoot 'tools/ymcc_sim/optimization/release-bug-gate.ps1')
      Invoke-YmccReleaseBugValidation -ProjectRoot $ProjectRoot -PlanPath $ReleaseBugPlan -BudgetSeconds $FixtureBudgetSeconds
    }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectRoot 'tools\package-release.ps1') -WorkspaceRoot $env:YEMAN_WORKSPACE_ROOT -FastRelease
  } elseif ($FullAcceptance) {
    . (Join-Path $ProjectRoot 'tools/ymcc_sim/optimization/release-full-budget.ps1')
    $env:YMCC_RELEASE_FIXTURE_SESSION = New-YmccFullReleaseFixtureSession -ProjectRoot $ProjectRoot -BudgetSeconds $FixtureBudgetSeconds
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectRoot 'tools\build-workspace.ps1') -WorkspaceRoot $env:YEMAN_WORKSPACE_ROOT -FullAcceptance
    if ($LASTEXITCODE -ne 0) { throw "Full acceptance build failed: exit=$LASTEXITCODE" }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectRoot 'tools\package-release.ps1') -WorkspaceRoot $env:YEMAN_WORKSPACE_ROOT -FullAcceptance
  } else {
    # Use the same original build/package entries as the other modes. Package-manager
    # run preflights can reinstall or purge an already restored dependency tree.
    if ([string]::IsNullOrWhiteSpace($env:YEMAN_WORKSPACE_ROOT)) {
      $env:YEMAN_WORKSPACE_ROOT = [IO.Path]::GetFullPath((Join-Path $ProjectRoot '..\..'))
    }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectRoot 'tools\build-workspace.ps1') -WorkspaceRoot $env:YEMAN_WORKSPACE_ROOT -TestExport
    if ($LASTEXITCODE -ne 0) { throw "Ordinary export build failed: exit=$LASTEXITCODE" }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectRoot 'tools\package-release.ps1') -WorkspaceRoot $env:YEMAN_WORKSPACE_ROOT -TestExport
  }
  if ($LASTEXITCODE -ne 0) {
    throw "Release build failed: exit=$LASTEXITCODE"
  }
  if ($DeployInstalled) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ProjectRoot 'tools\deploy-installed.ps1') -WorkspaceRoot $env:YEMAN_WORKSPACE_ROOT
    if ($LASTEXITCODE -ne 0) {
      throw "Installed deployment failed: exit=$LASTEXITCODE"
    }
  }
} finally {
  $env:YEMAN_WORKSPACE_ROOT = $previousWorkspaceRoot
  $env:YEMAN_TEST_EXPORT = $previousTestExport
  $env:YEMAN_FULL_ACCEPTANCE = $previousFullAcceptance
  $env:YMCC_RELEASE_FIXTURE_SESSION = $previousFixtureSession
  Pop-Location
}

if ($DeployInstalled) {
  Write-Output 'Release assembled and deployed to C:\SOFT\YeMan\YeManCC.'
} else {
  Write-Output 'Release assembled. No files were deployed to C:\SOFT\YeMan.'
}

}

# Dot-sourcing is the fixed owned worker's definition import, never an environment
# token that turns a normal -FullAcceptance invocation into an unguarded run.
if ($MyInvocation.InvocationName -ne '.') {
  if ($FullAcceptance) {
    if ($TestExport -or $FastRelease -or $ReleaseBugPlan) { throw 'FullAcceptance is separate from ordinary/Fast/ReleaseBugPlan.' }
    $publishStartedAtUnixMs=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    . (Join-Path $PSScriptRoot 'ymcc_sim/optimization/release-full-budget.ps1')
    Invoke-YmccFullReleasePublishInvocation -ProjectRoot (Split-Path -Parent $PSScriptRoot) -WorkspaceRoot $WorkspaceRoot -DeployInstalled:$DeployInstalled -BudgetSeconds $FixtureBudgetSeconds -StartedAtUnixMs $publishStartedAtUnixMs
    exit $LASTEXITCODE
  }
  Invoke-YmccPublishBody @PSBoundParameters
}
