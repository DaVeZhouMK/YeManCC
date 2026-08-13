[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Get-Sha256([string]$Path) {
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Assert-FileMatch([string]$Source, [string]$Destination, [string]$Label) {
  if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "$Label source missing" }
  if (-not (Test-Path -LiteralPath $Destination -PathType Leaf)) { throw "$Label destination missing" }
  if ((Get-Item -LiteralPath $Source).Length -ne (Get-Item -LiteralPath $Destination).Length) { throw "$Label size mismatch" }
  if ((Get-Sha256 $Source) -ne (Get-Sha256 $Destination)) { throw "$Label SHA256 mismatch" }
}

function Copy-TreeChecked([string]$Source, [string]$Destination) {
  New-Item -ItemType Directory -Path $Destination -Force | Out-Null
  foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
    Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $Destination $item.Name) -Recurse -Force
  }
}

$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('yeman-updater-rollback-' + [guid]::NewGuid().ToString('N'))
$programSource = Join-Path $testRoot 'package\YeManCC'
$powerControlSource = Join-Path $testRoot 'package\PowerControl'
$exeDir = Join-Path $testRoot 'installed\YeManCC'
$pcDir = Join-Path $testRoot 'installed\PowerControl'
$supportPath = Join-Path $exeDir 'YeMan-Support.html'
$rollbackRoot = Join-Path $testRoot 'rollback'
$rollbackFiles = Join-Path $rollbackRoot 'files'
$rollbackAddedFiles = New-Object 'System.Collections.Generic.List[string]'
$ordinaryRollbackPrepared = $false

function Register-FileForRollback([string]$SourceFile, [string]$SourceRoot, [string]$TargetRoot, [string]$Bucket) {
  $relative = $SourceFile.Substring($SourceRoot.Length).TrimStart('\')
  if ([string]::IsNullOrWhiteSpace($relative)) { throw 'rollback relative path is empty' }
  $target = Join-Path $TargetRoot $relative
  if (Test-Path -LiteralPath $target -PathType Container) { throw "update target is a directory but package entry is a file: $target" }
  if (Test-Path -LiteralPath $target -PathType Leaf) {
    $backupFile = Join-Path (Join-Path $rollbackFiles $Bucket) $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $backupFile) -Force | Out-Null
    Copy-Item -LiteralPath $target -Destination $backupFile -Force
    Assert-FileMatch $target $backupFile "rollback backup $Bucket\$relative"
  } else {
    $rollbackAddedFiles.Add($target) | Out-Null
  }
}

function Register-TreeForRollback([string]$SourceRoot, [string]$TargetRoot, [string]$Bucket, [string[]]$ExcludedTopLevel) {
  foreach ($item in Get-ChildItem -LiteralPath $SourceRoot -Recurse -File) {
    $relative = $item.FullName.Substring($SourceRoot.Length).TrimStart('\')
    $topLevel = ($relative -split '\\', 2)[0]
    if ($topLevel -in $ExcludedTopLevel) { continue }
    Register-FileForRollback $item.FullName $SourceRoot $TargetRoot $Bucket
  }
}

function Restore-OrdinaryFiles {
  if (-not $ordinaryRollbackPrepared) { return }
  for ($index = $rollbackAddedFiles.Count - 1; $index -ge 0; $index--) {
    $added = $rollbackAddedFiles[$index]
    if (Test-Path -LiteralPath $added -PathType Leaf) { Remove-Item -LiteralPath $added -Force }
    elseif (Test-Path -LiteralPath $added) { throw "rollback expected an added file but found another item: $added" }
  }
  $programBackup = Join-Path $rollbackFiles 'YeManCC'
  if (Test-Path -LiteralPath $programBackup -PathType Container) { Copy-TreeChecked $programBackup $exeDir }
  $powerControlBackup = Join-Path $rollbackFiles 'PowerControl'
  if (Test-Path -LiteralPath $powerControlBackup -PathType Container) { Copy-TreeChecked $powerControlBackup $pcDir }
  $supportBackup = Join-Path $rollbackFiles 'Support'
  if (Test-Path -LiteralPath $supportBackup -PathType Leaf) { Copy-Item -LiteralPath $supportBackup -Destination $supportPath -Force }
}

try {
  New-Item -ItemType Directory -Path $programSource, $powerControlSource, $exeDir, $pcDir, $rollbackFiles -Force | Out-Null
  New-Item -ItemType Directory -Path (Join-Path $programSource 'assets'), (Join-Path $powerControlSource 'Sleep') -Force | Out-Null
  Set-Content -LiteralPath (Join-Path $programSource 'index.html') -Value 'new-index'
  Set-Content -LiteralPath (Join-Path $programSource 'assets\new.js') -Value 'new-asset'
  Set-Content -LiteralPath (Join-Path $programSource 'YeMan-Support.html') -Value 'new-support'
  Set-Content -LiteralPath (Join-Path $powerControlSource 'template.ini') -Value 'new-template'
  Set-Content -LiteralPath (Join-Path $powerControlSource 'Sleep\exclude.txt') -Value 'new-exclude'
  Set-Content -LiteralPath (Join-Path $exeDir 'index.html') -Value 'old-index'
  Set-Content -LiteralPath $supportPath -Value 'old-support'
  Set-Content -LiteralPath (Join-Path $pcDir 'template.ini') -Value 'old-template'
  Set-Content -LiteralPath (Join-Path $pcDir 'player-owned.json') -Value 'keep-player-file'

  Register-TreeForRollback $programSource $exeDir 'YeManCC' @('YeManCC.exe', 'YeMan-Support.html')
  Register-TreeForRollback $powerControlSource $pcDir 'PowerControl' @('pawnio')
  Copy-Item -LiteralPath $supportPath -Destination (Join-Path $rollbackFiles 'Support') -Force
  $ordinaryRollbackPrepared = $true

  Copy-TreeChecked $programSource $exeDir
  Copy-TreeChecked $powerControlSource $pcDir
  Copy-Item -LiteralPath (Join-Path $programSource 'YeMan-Support.html') -Destination $supportPath -Force
  Restore-OrdinaryFiles

  if ((Get-Content -LiteralPath (Join-Path $exeDir 'index.html') -Raw).Trim() -ne 'old-index') { throw 'program overwrite was not restored' }
  if (Test-Path -LiteralPath (Join-Path $exeDir 'assets\new.js')) { throw 'new program file was not removed' }
  if ((Get-Content -LiteralPath $supportPath -Raw).Trim() -ne 'old-support') { throw 'support page was not restored' }
  if ((Get-Content -LiteralPath (Join-Path $pcDir 'template.ini') -Raw).Trim() -ne 'old-template') { throw 'PowerControl overwrite was not restored' }
  if (Test-Path -LiteralPath (Join-Path $pcDir 'Sleep\exclude.txt')) { throw 'new PowerControl file was not removed' }
  if ((Get-Content -LiteralPath (Join-Path $pcDir 'player-owned.json') -Raw).Trim() -ne 'keep-player-file') { throw 'player-owned file was changed' }

  Write-Output 'updater ordinary rollback self-test: PASS'
} finally {
  if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
