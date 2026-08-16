[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Get-Sha256([string]$Path) {
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Assert-Equal([object]$Actual, [object]$Expected, [string]$Label) {
  if ($Actual -ne $Expected) {
    throw "$Label mismatch. Expected '$Expected', got '$Actual'"
  }
}

function Assert-FileMatch([string]$Source, [string]$Destination, [string]$Label) {
  if (!(Test-Path -LiteralPath $Source -PathType Leaf)) { throw "$Label source missing: $Source" }
  if (!(Test-Path -LiteralPath $Destination -PathType Leaf)) { throw "$Label destination missing: $Destination" }
  Assert-Equal (Get-Sha256 $Destination) (Get-Sha256 $Source) "$Label SHA256"
}

function Get-RelativePath([string]$Root, [string]$Path) {
  return $Path.Substring($Root.Length).TrimStart('\')
}

function Copy-TreeChecked(
  [string]$Source,
  [string]$Destination,
  [string[]]$ExcludeTopLevel = @(),
  [string[]]$ExcludeFiles = @(),
  [switch]$Purge
) {
  if (!(Test-Path -LiteralPath $Source -PathType Container)) {
    throw "copy source is missing: $Source"
  }
  New-Item -ItemType Directory -Path $Destination -Force | Out-Null
  if ($Purge) {
    foreach ($existing in @(Get-ChildItem -LiteralPath $Destination -Force)) {
      Remove-Item -LiteralPath $existing.FullName -Recurse -Force
    }
  }
  foreach ($item in @(Get-ChildItem -LiteralPath $Source -Recurse -Force)) {
    $relative = Get-RelativePath $Source $item.FullName
    $topLevel = ($relative -split '\\', 2)[0]
    if ($topLevel -in $ExcludeTopLevel) { continue }
    if (!$item.PSIsContainer -and $item.Name -in $ExcludeFiles) { continue }
    $target = Join-Path $Destination $relative
    if ($item.PSIsContainer) {
      New-Item -ItemType Directory -Path $target -Force | Out-Null
    } else {
      New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
      Copy-Item -LiteralPath $item.FullName -Destination $target -Force
    }
  }
}

function Assert-TreeMatch([string]$Source, [string]$Destination, [string]$Label) {
  if (!(Test-Path -LiteralPath $Source -PathType Container)) { throw "$Label source directory missing" }
  if (!(Test-Path -LiteralPath $Destination -PathType Container)) { throw "$Label destination directory missing" }
  $sourceFiles = @(Get-ChildItem -LiteralPath $Source -Recurse -File | ForEach-Object { Get-RelativePath $Source $_.FullName })
  $destinationFiles = @(Get-ChildItem -LiteralPath $Destination -Recurse -File | ForEach-Object { Get-RelativePath $Destination $_.FullName })
  $missing = @($sourceFiles | Where-Object { $_ -notin $destinationFiles })
  $stale = @($destinationFiles | Where-Object { $_ -notin $sourceFiles })
  if ($missing.Count -gt 0) { throw "$Label destination missing files: $($missing -join ', ')" }
  if ($stale.Count -gt 0) { throw "$Label destination has stale files: $($stale -join ', ')" }
  foreach ($relative in $sourceFiles) {
    Assert-FileMatch (Join-Path $Source $relative) (Join-Path $Destination $relative) "$Label\$relative"
  }
}

function Write-Utf8Atomic([string]$Path, [string]$Text) {
  $parent = Split-Path -Parent $Path
  New-Item -ItemType Directory -Path $parent -Force | Out-Null
  $temporary = $Path + '.tmp-' + [guid]::NewGuid().ToString('N')
  $encoding = New-Object System.Text.UTF8Encoding -ArgumentList $false
  [IO.File]::WriteAllText($temporary, $Text, $encoding)
  Move-Item -LiteralPath $temporary -Destination $Path -Force
}

function Rename-DirectoryChecked([string]$Source, [string]$Destination, [string]$Label) {
  if ((Split-Path -Parent $Source) -ne (Split-Path -Parent $Destination)) {
    throw "$Label must stay in one parent directory"
  }
  if (!(Test-Path -LiteralPath $Source -PathType Container)) { throw "$Label source missing" }
  if (Test-Path -LiteralPath $Destination) { throw "$Label destination already exists" }
  Rename-Item -LiteralPath $Source -NewName (Split-Path -Leaf $Destination)
  if ((Test-Path -LiteralPath $Source) -or !(Test-Path -LiteralPath $Destination -PathType Container)) {
    throw "$Label rename verification failed"
  }
}

function Register-FileForRollback(
  [string]$SourceFile,
  [string]$SourceRoot,
  [string]$TargetRoot,
  [string]$RollbackFiles,
  [string]$Bucket,
  [System.Collections.Generic.List[string]]$AddedFiles
) {
  $relative = Get-RelativePath $SourceRoot $SourceFile
  if ([string]::IsNullOrWhiteSpace($relative)) { throw 'rollback relative path is empty' }
  $target = Join-Path $TargetRoot $relative
  if (Test-Path -LiteralPath $target -PathType Container) {
    throw "update target is a directory but package entry is a file: $target"
  }
  if (Test-Path -LiteralPath $target -PathType Leaf) {
    $backupFile = Join-Path (Join-Path $RollbackFiles $Bucket) $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $backupFile) -Force | Out-Null
    Copy-Item -LiteralPath $target -Destination $backupFile -Force
    Assert-FileMatch $target $backupFile "rollback backup $Bucket\$relative"
  } else {
    $AddedFiles.Add($target) | Out-Null
  }
}

function Register-TreeForRollback(
  [string]$SourceRoot,
  [string]$TargetRoot,
  [string]$RollbackFiles,
  [string]$Bucket,
  [string[]]$ExcludedTopLevel,
  [System.Collections.Generic.List[string]]$AddedFiles
) {
  foreach ($item in @(Get-ChildItem -LiteralPath $SourceRoot -Recurse -File)) {
    $relative = Get-RelativePath $SourceRoot $item.FullName
    $topLevel = ($relative -split '\\', 2)[0]
    if ($topLevel -in $ExcludedTopLevel) { continue }
    Register-FileForRollback $item.FullName $SourceRoot $TargetRoot $RollbackFiles $Bucket $AddedFiles
  }
}

function Restore-OrdinaryFiles(
  [string]$RollbackFiles,
  [string]$ExeDir,
  [string]$PcDir,
  [string]$SupportPath,
  [System.Collections.Generic.List[string]]$AddedFiles
) {
  for ($index = $AddedFiles.Count - 1; $index -ge 0; $index--) {
    $added = $AddedFiles[$index]
    if (Test-Path -LiteralPath $added -PathType Leaf) {
      Remove-Item -LiteralPath $added -Force
    } elseif (Test-Path -LiteralPath $added) {
      throw "rollback expected an added file but found another item: $added"
    }
  }
  $programBackup = Join-Path $RollbackFiles 'YeManCC'
  if (Test-Path -LiteralPath $programBackup -PathType Container) {
    Copy-TreeChecked $programBackup $ExeDir
  }
  $powerControlBackup = Join-Path $RollbackFiles 'PowerControl'
  if (Test-Path -LiteralPath $powerControlBackup -PathType Container) {
    Copy-TreeChecked $powerControlBackup $PcDir
  }
  $supportBackup = Join-Path $RollbackFiles 'Support'
  if (Test-Path -LiteralPath $supportBackup -PathType Leaf) {
    Copy-Item -LiteralPath $supportBackup -Destination $SupportPath -Force
  }
  foreach ($backupFile in @(Get-ChildItem -LiteralPath $RollbackFiles -Recurse -File)) {
    $relative = Get-RelativePath $RollbackFiles $backupFile.FullName
    if ($relative -eq 'Support') {
      $target = $SupportPath
    } elseif ($relative.StartsWith('YeManCC\')) {
      $target = Join-Path $ExeDir $relative.Substring('YeManCC\'.Length)
    } elseif ($relative.StartsWith('PowerControl\')) {
      $target = Join-Path $PcDir $relative.Substring('PowerControl\'.Length)
    } else {
      throw "unknown rollback bucket: $relative"
    }
    Assert-FileMatch $backupFile.FullName $target "rollback restored $relative"
  }
}

function New-OldInstall(
  [string]$PackageRoot,
  [string]$ExeDir,
  [string]$PcDir
) {
  Copy-TreeChecked (Join-Path $PackageRoot 'YeManCC') $ExeDir
  Copy-TreeChecked (Join-Path $PackageRoot 'PowerControl') $PcDir
  Set-Content -LiteralPath (Join-Path $ExeDir 'version.json') -Value '{"version":"0.0.10","notes":"old"}' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $ExeDir 'index.html') -Value 'old-index' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $ExeDir 'YeMan-Support.html') -Value 'old-support' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $PcDir 'Sleep\system-blacklist.txt') -Value 'old-system-rule' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $PcDir 'Sleep\player-blacklist.txt') -Value 'player-owned-rule' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $PcDir 'Sleep\player-owned.json') -Value 'keep-player-data' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $PcDir 'exclude.txt') -Value 'legacy-user-file' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $PcDir 'pawnio\old-runtime-marker.txt') -Value 'old-pawnio' -Encoding UTF8
  $removedAsset = Get-ChildItem -LiteralPath (Join-Path $ExeDir 'assets') -Recurse -File | Sort-Object FullName | Select-Object -First 1
  if (!$removedAsset) { throw 'test package has no web asset to exercise added-file rollback' }
  $removedRelative = Get-RelativePath $ExeDir $removedAsset.FullName
  Remove-Item -LiteralPath $removedAsset.FullName -Force
  return [ordered]@{
    RemovedProgramFile = $removedRelative
    OldExeSha256 = Get-Sha256 (Join-Path $ExeDir 'YeManCC.exe')
  }
}

function Invoke-InstallRound(
  [string]$PackageRoot,
  [string]$ScenarioRoot,
  [bool]$InjectFailure
) {
  $staging = Join-Path $ScenarioRoot 'staging'
  $exeDir = Join-Path $ScenarioRoot 'installed\YeManCC'
  $pcDir = Join-Path $ScenarioRoot 'installed\PowerControl'
  $statePath = Join-Path $ScenarioRoot 'update-state.json'
  $progressPath = Join-Path $ScenarioRoot 'update-progress.json'
  $rollbackRoot = Join-Path $ScenarioRoot 'rollback'
  $rollbackFiles = Join-Path $rollbackRoot 'files'
  $backup = Join-Path $exeDir 'YeManCC.exe.old'
  $newExe = Join-Path $exeDir 'YeManCC.exe.new'
  $supportPath = Join-Path $exeDir 'YeMan-Support.html'
  $programSource = Join-Path $staging 'YeManCC'
  $powerControlSource = Join-Path $staging 'PowerControl'
  $packageExe = Join-Path $programSource 'YeManCC.exe'
  $systemBlacklistSource = Join-Path $powerControlSource 'Sleep\system-blacklist.txt'
  $systemBlacklistPath = Join-Path $pcDir 'Sleep\system-blacklist.txt'
  $playerBlacklistPath = Join-Path $pcDir 'Sleep\player-blacklist.txt'
  $playerBlacklistRollback = Join-Path $rollbackFiles 'PowerControl\Sleep\player-blacklist.txt'
  $tdpSource = Join-Path $powerControlSource 'pawnio'
  $tdpStage = Join-Path $pcDir 'pawnio.update-test'
  $tdpBackup = Join-Path $pcDir 'pawnio.rollback-test'
  $addedFiles = New-Object 'System.Collections.Generic.List[string]'
  $ordinaryPrepared = $false
  $tdpOriginalMoved = $false
  $tdpCommitted = $false
  $updateCommitted = $false
  $rollbackSucceeded = $false
  $failureMessage = ''

  try {
    Write-Utf8Atomic $statePath '{"phase":"copying"}'
    Write-Utf8Atomic $progressPath '{"phase":"installing","stage":"install"}'
    New-Item -ItemType Directory -Path $rollbackFiles -Force | Out-Null
    Register-TreeForRollback $programSource $exeDir $rollbackFiles 'YeManCC' @('YeManCC.exe','YeMan-Support.html') $addedFiles
    Register-FileForRollback $packageExe $programSource $exeDir $rollbackFiles 'YeManCC' $addedFiles
    Register-TreeForRollback $powerControlSource $pcDir $rollbackFiles 'PowerControl' @('pawnio') $addedFiles
    if (Test-Path -LiteralPath $supportPath -PathType Leaf) {
      Copy-Item -LiteralPath $supportPath -Destination (Join-Path $rollbackFiles 'Support') -Force
      Assert-FileMatch $supportPath (Join-Path $rollbackFiles 'Support') 'support rollback backup'
    } else {
      $addedFiles.Add($supportPath) | Out-Null
    }
    $ordinaryPrepared = $true

    Copy-TreeChecked $powerControlSource $pcDir @('pawnio')
    if (!(Test-Path -LiteralPath $systemBlacklistSource -PathType Leaf)) { throw 'system blacklist missing from package' }
    Copy-Item -LiteralPath $systemBlacklistSource -Destination $systemBlacklistPath -Force
    Assert-FileMatch $systemBlacklistSource $systemBlacklistPath 'system blacklist update'
    if (Test-Path -LiteralPath $playerBlacklistPath -PathType Leaf) {
      if (!(Test-Path -LiteralPath $playerBlacklistRollback -PathType Leaf)) { throw 'player blacklist rollback backup missing' }
      Copy-Item -LiteralPath $playerBlacklistRollback -Destination $playerBlacklistPath -Force
      Assert-FileMatch $playerBlacklistRollback $playerBlacklistPath 'player blacklist preservation'
    }

    Copy-TreeChecked $tdpSource $tdpStage @() @() -Purge
    Assert-TreeMatch $tdpSource $tdpStage 'PawnIO staged runtime'
    if (Test-Path -LiteralPath $tdpBackup) { Remove-Item -LiteralPath $tdpBackup -Recurse -Force }
    $tdpTarget = Join-Path $pcDir 'pawnio'
    if (Test-Path -LiteralPath $tdpTarget -PathType Container) {
      Rename-DirectoryChecked $tdpTarget $tdpBackup 'PawnIO backup'
      $tdpOriginalMoved = $true
    }
    Rename-DirectoryChecked $tdpStage $tdpTarget 'PawnIO commit'
    $tdpCommitted = $true
    Assert-TreeMatch $tdpSource $tdpTarget 'PawnIO committed runtime'
    Write-Utf8Atomic $statePath '{"phase":"tdp-verified"}'

    if (Test-Path -LiteralPath (Join-Path $exeDir 'YeManCC.exe') -PathType Leaf) {
      Copy-Item -LiteralPath (Join-Path $exeDir 'YeManCC.exe') -Destination $backup -Force
    }
    Copy-Item -LiteralPath $packageExe -Destination $newExe -Force
    Move-Item -LiteralPath $newExe -Destination (Join-Path $exeDir 'YeManCC.exe') -Force
    Copy-TreeChecked $programSource $exeDir @() @('YeManCC.exe','YeMan-Support.html')
    Copy-Item -LiteralPath (Join-Path $programSource 'YeMan-Support.html') -Destination $supportPath -Force

    if ($InjectFailure) { throw 'simulated startup handshake failure' }

    $handshakePath = Join-Path $ScenarioRoot 'update-handshake-test.json'
    $handshakeToken = 'test-handshake-token'
    $handshake = [ordered]@{ phase = 'started'; pid = $PID; token = $handshakeToken }
    Write-Utf8Atomic $handshakePath ($handshake | ConvertTo-Json -Compress)
    $marker = Get-Content -LiteralPath $handshakePath -Raw | ConvertFrom-Json
    if ($marker.phase -ne 'started' -or [int]$marker.pid -ne $PID -or [string]$marker.token -ne $handshakeToken) {
      throw 'simulated startup handshake validation failed'
    }
    $updateCommitted = $true
    Write-Utf8Atomic $statePath ('{"phase":"started","pid":' + $PID + '}')
    Write-Utf8Atomic $progressPath '{"phase":"completed","stage":"install"}'
  } catch {
    $failureMessage = $_.Exception.Message
    if (!$updateCommitted -and ($tdpCommitted -or $tdpOriginalMoved)) {
      try {
        $tdpTarget = Join-Path $pcDir 'pawnio'
        if (Test-Path -LiteralPath $tdpTarget) { Remove-Item -LiteralPath $tdpTarget -Recurse -Force }
        if (Test-Path -LiteralPath $tdpBackup -PathType Container) {
          Rename-DirectoryChecked $tdpBackup $tdpTarget 'PawnIO rollback'
        } elseif ($tdpOriginalMoved) {
          throw 'PawnIO rollback directory missing'
        }
      } catch {
        $failureMessage += '; PawnIO rollback: ' + $_.Exception.Message
      }
    }
    if (!$updateCommitted) {
      $rollbackError = $null
      try { Restore-OrdinaryFiles $rollbackFiles $exeDir $pcDir $supportPath $addedFiles } catch { $rollbackError = 'ordinary rollback: ' + $_.Exception.Message }
      try {
        if (Test-Path -LiteralPath $backup -PathType Leaf) {
          Copy-Item -LiteralPath $backup -Destination (Join-Path $exeDir 'YeManCC.exe') -Force
        }
      } catch {
        if ($rollbackError) { $rollbackError += '; ' }
        $rollbackError += 'EXE rollback: ' + $_.Exception.Message
      }
      if ($rollbackError) { $failureMessage += '; ' + $rollbackError } else { $rollbackSucceeded = $true }
      Write-Utf8Atomic $progressPath ('{"phase":"failed","stage":"install","error":' + (ConvertTo-Json $failureMessage -Compress) + '}')
      Write-Utf8Atomic $statePath ('{"phase":"rolled-back","error":' + (ConvertTo-Json $failureMessage -Compress) + '}')
    }
  } finally {
    foreach ($cleanupPath in @($newExe, $tdpStage)) {
      if (Test-Path -LiteralPath $cleanupPath) { Remove-Item -LiteralPath $cleanupPath -Recurse -Force }
    }
    if ($updateCommitted -or $rollbackSucceeded) {
      foreach ($cleanupPath in @($backup, $tdpBackup, $rollbackRoot)) {
        if (Test-Path -LiteralPath $cleanupPath) { Remove-Item -LiteralPath $cleanupPath -Recurse -Force }
      }
    }
    if ($updateCommitted -or $rollbackSucceeded) {
      if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
    }
  }

  return [ordered]@{
    Committed = $updateCommitted
    RollbackSucceeded = $rollbackSucceeded
    FailureMessage = $failureMessage
    ExeDir = $exeDir
    PcDir = $pcDir
    StatePath = $statePath
    ProgressPath = $progressPath
    Staging = $staging
    RollbackRoot = $rollbackRoot
    TdpBackup = $tdpBackup
  }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '..\..'))
$package = Join-Path $workspaceRoot 'Release\Packages\YeManCC.zip'
$releaseManifest = Join-Path $workspaceRoot 'Release\version.json'
if (!(Test-Path -LiteralPath $package -PathType Leaf)) { throw "release package missing: $package" }
if (!(Test-Path -LiteralPath $releaseManifest -PathType Leaf)) { throw "release manifest missing: $releaseManifest" }
$manifestText = (Get-Content -LiteralPath $releaseManifest -Raw -Encoding UTF8).TrimStart([char]0xFEFF)
$manifest = $manifestText | ConvertFrom-Json
Assert-Equal (Get-Sha256 $package) ([string]$manifest.sha256) 'release package SHA256'

$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
$testRoot = Join-Path $tempRoot ('yeman-updater-install-chain-' + [guid]::NewGuid().ToString('N'))
if (-not ([IO.Path]::GetFullPath($testRoot).StartsWith($tempRoot + '\', [StringComparison]::OrdinalIgnoreCase))) {
  throw "unsafe test root: $testRoot"
}

try {
  New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
  $packageRoot = Join-Path $testRoot 'package'
  Expand-Archive -LiteralPath $package -DestinationPath $packageRoot -Force
  $topLevel = @(Get-ChildItem -LiteralPath $packageRoot -Directory | Select-Object -ExpandProperty Name | Sort-Object)
  if ($topLevel.Count -ne 2 -or $topLevel[0] -ne 'PowerControl' -or $topLevel[1] -ne 'YeManCC') {
    throw "package root is not exactly PowerControl + YeManCC: $($topLevel -join ', ')"
  }
  if (Get-ChildItem -LiteralPath $packageRoot -Recurse -File -Filter 'exclude.txt') {
    throw 'obsolete exclude.txt entered the update package'
  }
  Assert-Equal ((Get-Content -LiteralPath (Join-Path $packageRoot 'YeManCC\version.json') -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json).version ([string]$manifest.version) 'packaged version'
  foreach ($required in @(
    'YeManCC\YeManCC.exe',
    'YeManCC\index.html',
    'YeManCC\YeMan-Support.html',
    'PowerControl\Sleep\system-blacklist.txt',
    'PowerControl\pawnio\YeManTdpCtl.exe',
    'PowerControl\pawnio\_internal'
  )) {
    if (!(Test-Path -LiteralPath (Join-Path $packageRoot $required))) { throw "required package item missing: $required" }
  }

  $successRoot = Join-Path $testRoot 'success'
  $successExe = Join-Path $successRoot 'installed\YeManCC'
  $successPc = Join-Path $successRoot 'installed\PowerControl'
  New-Item -ItemType Directory -Path $successExe, $successPc | Out-Null
  $successBaseline = New-OldInstall $packageRoot $successExe $successPc
  Copy-TreeChecked $packageRoot (Join-Path $successRoot 'staging')
  $successResult = Invoke-InstallRound $packageRoot $successRoot $false
  if (!$successResult.Committed) { throw "success round did not commit: $($successResult.FailureMessage)" }
  Assert-Equal ((Get-Content -LiteralPath (Join-Path $successResult.ExeDir 'version.json') -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json).version ([string]$manifest.version) 'success installed version'
  Assert-FileMatch (Join-Path $packageRoot 'YeManCC\index.html') (Join-Path $successResult.ExeDir 'index.html') 'success program files'
  Assert-FileMatch (Join-Path $packageRoot 'YeManCC\YeMan-Support.html') (Join-Path $successResult.ExeDir 'YeMan-Support.html') 'success support page'
  Assert-FileMatch (Join-Path $packageRoot 'PowerControl\Sleep\system-blacklist.txt') (Join-Path $successResult.PcDir 'Sleep\system-blacklist.txt') 'success system blacklist'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $successResult.PcDir 'Sleep\player-blacklist.txt') -Raw).Trim() 'player-owned-rule' 'success player blacklist'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $successResult.PcDir 'Sleep\player-owned.json') -Raw).Trim() 'keep-player-data' 'success player data'
  if (!(Test-Path -LiteralPath (Join-Path $successResult.PcDir 'exclude.txt') -PathType Leaf)) { throw 'existing user exclude.txt was unexpectedly deleted' }
  if (Test-Path -LiteralPath (Join-Path $successResult.PcDir 'pawnio\old-runtime-marker.txt')) { throw 'old PawnIO runtime leaked into successful update' }
  Assert-TreeMatch (Join-Path $packageRoot 'PowerControl\pawnio') (Join-Path $successResult.PcDir 'pawnio') 'success PawnIO runtime'
  if (Test-Path -LiteralPath $successResult.Staging) { throw 'success staging directory was not cleaned' }
  if (Test-Path -LiteralPath $successResult.RollbackRoot) { throw 'success rollback directory was not cleaned' }
  $successProgress = Get-Content -LiteralPath $successResult.ProgressPath -Raw -Encoding UTF8 | ConvertFrom-Json
  Assert-Equal $successProgress.phase 'completed' 'success progress phase'

  $failureRoot = Join-Path $testRoot 'failure'
  $failureExe = Join-Path $failureRoot 'installed\YeManCC'
  $failurePc = Join-Path $failureRoot 'installed\PowerControl'
  New-Item -ItemType Directory -Path $failureExe, $failurePc | Out-Null
  $failureBaseline = New-OldInstall $packageRoot $failureExe $failurePc
  Copy-TreeChecked $packageRoot (Join-Path $failureRoot 'staging')
  $oldPawnioSnapshot = Join-Path $failureRoot 'expected-old-pawnio'
  Copy-TreeChecked (Join-Path $failurePc 'pawnio') $oldPawnioSnapshot
  $failureResult = Invoke-InstallRound $packageRoot $failureRoot $true
  if (!$failureResult.RollbackSucceeded) { throw "failure round did not complete rollback: $($failureResult.FailureMessage)" }
  Assert-Equal ((Get-Content -LiteralPath (Join-Path $failureResult.ExeDir 'version.json') -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json).version '0.0.10' 'rollback installed version'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $failureResult.ExeDir 'index.html') -Raw).Trim() 'old-index' 'rollback program file'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $failureResult.ExeDir 'YeMan-Support.html') -Raw).Trim() 'old-support' 'rollback support page'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $failureResult.PcDir 'Sleep\system-blacklist.txt') -Raw).Trim() 'old-system-rule' 'rollback system blacklist'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $failureResult.PcDir 'Sleep\player-blacklist.txt') -Raw).Trim() 'player-owned-rule' 'rollback player blacklist'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $failureResult.PcDir 'Sleep\player-owned.json') -Raw).Trim() 'keep-player-data' 'rollback player data'
  Assert-TreeMatch $oldPawnioSnapshot (Join-Path $failureResult.PcDir 'pawnio') 'rollback PawnIO runtime'
  if (Test-Path -LiteralPath (Join-Path $failureResult.ExeDir $failureBaseline.RemovedProgramFile)) { throw 'rollback did not remove package-only program file' }
  if (Test-Path -LiteralPath $failureResult.Staging) { throw 'failure staging directory was not cleaned' }
  if (Test-Path -LiteralPath $failureResult.RollbackRoot) { throw 'failure rollback directory was not cleaned' }
  $failureState = Get-Content -LiteralPath $failureResult.StatePath -Raw -Encoding UTF8 | ConvertFrom-Json
  $failureProgress = Get-Content -LiteralPath $failureResult.ProgressPath -Raw -Encoding UTF8 | ConvertFrom-Json
  Assert-Equal $failureState.phase 'rolled-back' 'rollback state phase'
  Assert-Equal $failureProgress.phase 'failed' 'rollback progress phase'
  $recovery = Start-Process -FilePath $env:ComSpec -WorkingDirectory $failureResult.ExeDir -ArgumentList @('/c', 'exit 0') -WindowStyle Hidden -PassThru
  $recovery.WaitForExit()
  Assert-Equal $recovery.ExitCode 0 'rollback recovery process'

  Write-Output 'updater install chain self-test: PASS'
  Write-Output 'success round: committed, system blacklist updated, player blacklist preserved, PawnIO replaced'
  Write-Output 'failure round: injected handshake failure, ordinary files and PawnIO rolled back, recovery process launched'
} finally {
  if (Test-Path -LiteralPath $testRoot) {
    Remove-Item -LiteralPath $testRoot -Recurse -Force
  }
}
