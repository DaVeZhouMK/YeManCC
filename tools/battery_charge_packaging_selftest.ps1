[CmdletBinding()]
param([string]$WorkspaceRoot=$env:YEMAN_WORKSPACE_ROOT, [switch]$StaticOnly)
$ErrorActionPreference='Stop'
$ProjectRoot=Split-Path -Parent $PSScriptRoot
if(-not $WorkspaceRoot){throw 'Pass the isolated workspace used by build-battery-host.ps1'}
# Parse definitions only. Never execute the package/deploy script bodies.
foreach($item in @(
  @{Path='build-battery-host.ps1';Name='Remove-BatteryHostLegacyAppHost'},
  @{Path='package-release.ps1';Name='Test-IsExcludedPowerControlPath'},
  @{Path='package-release.ps1';Name='Get-RelativePath'},
  @{Path='package-release.ps1';Name='Get-FullPath'},
  @{Path='deploy-installed.ps1';Name='Test-InstalledPowerControlExcluded'})) {
  $parseErrors=$null;$tokens=$null
  $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $item.Path),[ref]$tokens,[ref]$parseErrors)
  if($parseErrors.Count){throw ($parseErrors | Out-String)}
  $definition=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $item.Name},$true)
  if(-not $definition){throw "Missing packaging function $($item.Name)"}
  Invoke-Expression $definition.Extent.Text
}
$allowed=(Get-Content -LiteralPath (Join-Path $ProjectRoot 'native/BatteryHost/payload-files.json') -Raw | ConvertFrom-Json)
foreach($name in $allowed){
  $relative='battery-host\'+$name
  if((Test-IsExcludedPowerControlPath $relative $false) -or (Test-InstalledPowerControlExcluded $relative $false)){throw "Required payload excluded: $relative"}
}
foreach($name in @('YeManBatteryHost.exe','battery-charge.settings.json','battery-charge.recovery.json','random.dll','YeManBatteryHost.pdb','subdir\YeManBatteryHost.exe')){
  $relative='battery-host\'+$name
  if(-not (Test-IsExcludedPowerControlPath $relative $false) -or -not (Test-InstalledPowerControlExcluded $relative $false)){throw "Unexpected battery payload admitted: $relative"}
}
# Exercise the production cleanup function using only tiny isolated evidence files.
$cleanupRoot=Join-Path $WorkspaceRoot ('Testing\YMCC\Evidence\BatteryHostPackaging-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $cleanupRoot -Force | Out-Null
foreach($relative in @('Build\App\BatteryHost','PowerControl\battery-host')) {
  $fixture=Join-Path $cleanupRoot $relative
  New-Item -ItemType Directory -Path (Join-Path $fixture 'subdir') -Force | Out-Null
  foreach($name in @('YeManBatteryHost.exe','battery-charge.settings.json','battery-charge.recovery.json','YeManBatteryHost.dll','subdir\YeManBatteryHost.exe')) {
    [IO.File]::WriteAllText((Join-Path $fixture $name),'preserve-except-exact-legacy-apphost')
  }
  $rejected=$false
  try { Remove-BatteryHostLegacyAppHost $fixture -RejectOnly } catch { $rejected=$true }
  if(-not $rejected) { throw 'Legacy apphost residue was admitted' }
  Remove-BatteryHostLegacyAppHost $fixture
  Remove-BatteryHostLegacyAppHost $fixture
  Remove-BatteryHostLegacyAppHost $fixture -RejectOnly
  foreach($name in @('battery-charge.settings.json','battery-charge.recovery.json','YeManBatteryHost.dll','subdir\YeManBatteryHost.exe')) {
    if([IO.File]::ReadAllText((Join-Path $fixture $name)) -cne 'preserve-except-exact-legacy-apphost') { throw "Cleanup changed unrelated payload/state: $name" }
  }
  New-Item -ItemType Directory -Path (Join-Path $fixture 'YeManBatteryHost.exe') | Out-Null
  $rejected=$false
  try { Remove-BatteryHostLegacyAppHost $fixture } catch { $rejected=$true }
  if(-not $rejected -or -not (Test-Path -LiteralPath (Join-Path $fixture 'YeManBatteryHost.exe') -PathType Container)) { throw 'Legacy apphost directory was removed or admitted' }
}
Write-Output "BATTERY_LEGACY_APPHOST_CLEANUP_OK: exact files removed; state/nested files preserved; residue/directories rejected. Evidence: $cleanupRoot"
if($StaticOnly) {
  Write-Output "BATTERY_PACKAGE_STATIC_OK: $($allowed.Count) allowlisted files admitted; legacy apphost/state/foreign payload rejected. Build identity and final staging scan NOT_RUN."
  return
}
. (Join-Path $PSScriptRoot 'release-export-identity.ps1')
$payload=@(Get-BatteryHostBuildPayload $WorkspaceRoot $ProjectRoot)
if($payload.Count -ne $allowed.Count){throw 'Battery export coverage differs from allowlist'}
$manifest=Get-Content -LiteralPath (Join-Path $ProjectRoot 'PowerControl/battery-host/battery-host.payload.json') -Raw | ConvertFrom-Json
foreach($file in $manifest.files){
  $actual=Get-ExportFileHash (Join-Path $ProjectRoot ('PowerControl/battery-host/'+$file.path))
  if($actual -ne $file.sha256){throw "Battery manifest hash mismatch: $($file.path)"}
}
Write-Output "BATTERY_PACKAGE_CONTRACT_OK: $($payload.Count) immutable files, source/build hashes match; runtime state rejected. No deployment."

# Exercise the real final staging scan too: its generic document filter must agree with copy policy.
$packageSource=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'package-release.ps1') -Raw -Encoding UTF8
$scanStart=$packageSource.IndexOf('$forbidden = @()')
$scanEnd=$packageSource.IndexOf('if ($forbidden.Count -gt 0)', $scanStart)
if($scanStart -lt 0 -or $scanEnd -lt 0){throw 'Missing final staging scanner'}
$scan=[scriptblock]::Create($packageSource.Substring($scanStart,$scanEnd-$scanStart)+'; $forbidden')
$StagingRoot=Join-Path $WorkspaceRoot ('Build\Validation\BatteryPackageFixture-'+[Guid]::NewGuid().ToString('N'))
$batteryFixture=Join-Path $StagingRoot 'PowerControl\battery-host'
New-Item -ItemType Directory -Force -Path $batteryFixture | Out-Null
foreach($name in $allowed){[IO.File]::WriteAllText((Join-Path $batteryFixture $name),'')}
$found=@(& $scan)
if($found.Count){throw "Battery payload rejected by final scanner: $($found -join ', ')"}
foreach($name in @('foreign.md','debug.pdb')){[IO.File]::WriteAllText((Join-Path $batteryFixture $name),'')}
$found=@(& $scan)
if($found.Count -ne 2){throw 'Final scanner did not reject unrelated docs/debug symbols'}
Write-Output 'BATTERY_FINAL_STAGING_SCAN_OK: exact payload admitted; foreign documents and debug symbols rejected.'
