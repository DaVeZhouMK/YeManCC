[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$mainSource = Join-Path $PSScriptRoot '..'
$mainNativeSource = Join-Path $mainSource 'native\main.cpp'
if (-not (Test-Path -LiteralPath $mainNativeSource -PathType Leaf)) { throw 'main native source missing' }
$mainNativeText = Get-Content -LiteralPath $mainNativeSource -Raw
if ($mainNativeText -notmatch 'pressed\(XINPUT_GAMEPAD_X\)\) gamepadEmitUiAction\("edit-game"\)') {
  throw 'CustomSteamLibrary edit action is not bound to controller X in the mainline input bridge'
}
if ($mainNativeText -match 'pressed\(XINPUT_GAMEPAD_Y\)\) gamepadEmitUiAction\("edit-game"\)') {
  throw 'CustomSteamLibrary edit action is incorrectly bound to controller Y'
}

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

function Get-CustomManagedPaths([string]$SourceRoot) {
  $manifestPath = Join-Path $SourceRoot 'package-manifest.json'
  if (!(Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'CustomSteamLibrary package-manifest.json missing' }
  $manifest = (Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json
  if ([string]$manifest.packageId -ne 'custom-steam-library' -or [string]$manifest.packageType -ne 'green-child') { throw 'CustomSteamLibrary package identity is invalid' }
  if ([string]$manifest.entryPoint -ne 'CustomSteamLibrary.exe' -or [string]$manifest.worker -ne 'SteamArtworkLab.exe') { throw 'CustomSteamLibrary entry points are invalid' }
  if ([string]$manifest.updater.unknownPaths -ne 'preserve') { throw 'CustomSteamLibrary unknown-path policy is not preserve' }
  if ([int]$manifest.updater.healthHandshake.protocol -ne 1 -or [bool]$manifest.updater.healthHandshake.requiredBeforeCommit -ne $true) { throw 'CustomSteamLibrary health handshake policy is missing' }
  $normalize = {
    param([string]$Path)
    $normalized = $Path.Replace('/', [IO.Path]::DirectorySeparatorChar)
    if ([string]::IsNullOrWhiteSpace($normalized) -or [IO.Path]::IsPathRooted($normalized) -or $normalized.Contains(':') -or @($normalized -split '\\' | Where-Object { $_ -eq '..' -or $_ -eq '' }).Count -gt 0) {
      throw "unsafe CustomSteamLibrary path: $Path"
    }
    return $normalized
  }
  $managedRaw = @($manifest.managedPaths | ForEach-Object { & $normalize ([string]$_) })
  $managed = @($managedRaw | Sort-Object -Unique)
  $filesRaw = @($manifest.files | ForEach-Object { & $normalize ([string]$_) })
  $files = @($filesRaw | Sort-Object -Unique)
  if ($managed.Count -ne $managedRaw.Count -or $files.Count -ne $filesRaw.Count -or (Compare-Object $files $managed)) { throw 'CustomSteamLibrary files and managedPaths are inconsistent' }
  $paths = @($managed + 'package-manifest.json' | Sort-Object -Unique)
  foreach ($relative in $paths) {
    if (!(Test-Path -LiteralPath (Join-Path $SourceRoot $relative) -PathType Leaf)) { throw "CustomSteamLibrary managed file missing: $relative" }
  }
  $actual = @(Get-ChildItem -LiteralPath $SourceRoot -Recurse -File -Force | ForEach-Object { Get-RelativePath $SourceRoot $_.FullName } | Sort-Object -Unique)
  if (Compare-Object $paths $actual) { throw 'CustomSteamLibrary package contains an unlisted file or misses a managed file' }
  $indexed = @($manifest.fileIndex)
  if ($indexed.Count -ne $files.Count) { throw 'CustomSteamLibrary fileIndex is incomplete' }
  $indexedRaw = @($indexed | ForEach-Object { & $normalize ([string]$_.path) })
  $indexedPaths = @($indexedRaw | Sort-Object -Unique)
  if ($indexedPaths.Count -ne $indexedRaw.Count -or (Compare-Object $files $indexedPaths)) { throw 'CustomSteamLibrary fileIndex does not exactly cover files' }
  foreach ($entry in $indexed) {
    $relative = & $normalize ([string]$entry.path)
    if ($relative -notin $paths) { throw "CustomSteamLibrary fileIndex contains an unmanaged path: $relative" }
    $file = Join-Path $SourceRoot $relative
    $item = Get-Item -LiteralPath $file
    if ($item.Length -ne [int64]$entry.bytes) { throw "CustomSteamLibrary fileIndex byte mismatch: $relative" }
    if ((Get-Sha256 $file).ToLowerInvariant() -ne ([string]$entry.sha256).ToLowerInvariant()) { throw "CustomSteamLibrary fileIndex SHA256 mismatch: $relative" }
  }
  return $paths
}

function Register-CustomManagedFilesForRollback(
  [string]$SourceRoot,
  [string]$TargetRoot,
  [string]$RollbackFiles,
  [string[]]$Paths,
  [System.Collections.Generic.List[string]]$AddedFiles
) {
  foreach ($relative in $Paths) {
    Register-FileForRollback (Join-Path $SourceRoot $relative) $SourceRoot $TargetRoot $RollbackFiles 'CustomSteamLibrary' $AddedFiles
  }
}

function Copy-CustomManagedFilesChecked([string]$SourceRoot, [string]$TargetRoot, [string[]]$Paths) {
  New-Item -ItemType Directory -Path $TargetRoot -Force | Out-Null
  foreach ($relative in $Paths) {
    $source = Join-Path $SourceRoot $relative
    $target = Join-Path $TargetRoot $relative
    if (Test-Path -LiteralPath $target -PathType Container) { throw "CustomSteamLibrary target is a directory but package entry is a file: $relative" }
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Force
    Assert-FileMatch $source $target "CustomSteamLibrary managed file $relative"
  }
}

function Restore-OrdinaryFiles(
  [string]$RollbackFiles,
  [string]$ExeDir,
  [string]$PcDir,
  [string]$CustomDir,
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
  $customBackup = Join-Path $RollbackFiles 'CustomSteamLibrary'
  if (Test-Path -LiteralPath $customBackup -PathType Container) {
    Copy-TreeChecked $customBackup $CustomDir
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
    } elseif ($relative.StartsWith('CustomSteamLibrary\')) {
      $target = Join-Path $CustomDir $relative.Substring('CustomSteamLibrary\'.Length)
    } else {
      throw "unknown rollback bucket: $relative"
    }
    Assert-FileMatch $backupFile.FullName $target "rollback restored $relative"
  }
}

function New-OldInstall(
  [string]$PackageRoot,
  [string]$ExeDir,
  [string]$PcDir,
  [string]$CustomDir
) {
  Copy-TreeChecked (Join-Path $PackageRoot 'YeManCC') $ExeDir
  Copy-TreeChecked (Join-Path $PackageRoot 'PowerControl') $PcDir
  Copy-TreeChecked (Join-Path $PackageRoot 'CustomSteamLibrary') $CustomDir
  Set-Content -LiteralPath (Join-Path $ExeDir 'version.json') -Value '{"version":"0.0.10","notes":"old"}' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $ExeDir 'index.html') -Value 'old-index' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $ExeDir 'YeMan-Support.html') -Value 'old-support' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $PcDir 'Sleep\system-blacklist.txt') -Value 'old-system-rule' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $PcDir 'Sleep\player-blacklist.txt') -Value 'player-owned-rule' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $PcDir 'Sleep\player-owned.json') -Value 'keep-player-data' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $PcDir 'exclude.txt') -Value 'legacy-user-file' -Encoding UTF8
  New-Item -ItemType Directory -Path (Join-Path $PcDir 'fan-host') -Force | Out-Null
  Set-Content -LiteralPath (Join-Path $PcDir 'fan-host\old-host-marker.txt') -Value 'keep-old-fan-host' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $PcDir 'pawnio\old-runtime-marker.txt') -Value 'old-pawnio' -Encoding UTF8
  New-Item -ItemType Directory -Path (Join-Path $CustomDir 'data\config') -Force | Out-Null
  Set-Content -LiteralPath (Join-Path $CustomDir 'data\config\user.json') -Value 'keep-custom-data' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $CustomDir 'user-owned.txt') -Value 'keep-custom-unknown' -Encoding UTF8
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
  [ValidateSet('', 'custom-health', 'main-health')]
  [string]$FailureStage = ''
) {
  $staging = Join-Path $ScenarioRoot 'staging'
  $exeDir = Join-Path $ScenarioRoot 'installed\YeManCC'
  $pcDir = Join-Path $ScenarioRoot 'installed\PowerControl'
  $customDir = Join-Path $ScenarioRoot 'installed\CustomSteamLibrary'
  $statePath = Join-Path $ScenarioRoot 'update-state.json'
  $progressPath = Join-Path $ScenarioRoot 'update-progress.json'
  $rollbackRoot = Join-Path $ScenarioRoot 'rollback'
  $rollbackFiles = Join-Path $rollbackRoot 'files'
  $backup = Join-Path $exeDir 'YeManCC.exe.old'
  $newExe = Join-Path $exeDir 'YeManCC.exe.new'
  $supportPath = Join-Path $exeDir 'YeMan-Support.html'
  $programSource = Join-Path $staging 'YeManCC'
  $powerControlSource = Join-Path $staging 'PowerControl'
  $customSource = Join-Path $staging 'YeManCC\CustomSteamLibrary'
  $customManagedPaths = @(Get-CustomManagedPaths $customSource)
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
    Register-CustomManagedFilesForRollback $customSource $customDir $rollbackFiles $customManagedPaths $addedFiles
    if (Test-Path -LiteralPath $supportPath -PathType Leaf) {
      Copy-Item -LiteralPath $supportPath -Destination (Join-Path $rollbackFiles 'Support') -Force
      Assert-FileMatch $supportPath (Join-Path $rollbackFiles 'Support') 'support rollback backup'
    } else {
      $addedFiles.Add($supportPath) | Out-Null
    }
    $ordinaryPrepared = $true

    Copy-CustomManagedFilesChecked $customSource $customDir $customManagedPaths
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

    $customHealthPath = Join-Path $ScenarioRoot 'custom-health-test.json'
    $customHealthToken = 'custom-health-token'
    if ($FailureStage -eq 'custom-health') {
      Write-Utf8Atomic $customHealthPath (@{ phase = 'failed'; token = $customHealthToken; error = 'simulated CustomSteamLibrary WebView2 failure' } | ConvertTo-Json -Compress)
      throw 'simulated CustomSteamLibrary startup health failure'
    }
    $customHealthMarker = [ordered]@{
      phase = 'ready'
      schemaVersion = 1
      packageId = 'custom-steam-library'
      packageType = 'green-child'
      packageVersion = [string]$manifest.version
      expectedPackageVersion = [string]$manifest.version
      token = $customHealthToken
      pid = $PID
      inputOwner = 'host'
      healthOnly = $true
      protocol = 1
      window = $true
      worker = $true
      workspaceUi = $true
      webview2 = $true
      dataRootWritable = $true
      dataRoot = 'C:\SOFT\YeMan\CustomSteamLibrary\data'
    }
    Write-Utf8Atomic $customHealthPath ($customHealthMarker | ConvertTo-Json -Compress)
    $readCustomHealth = Get-Content -LiteralPath $customHealthPath -Raw | ConvertFrom-Json
    if ([string]$readCustomHealth.phase -ne 'ready' -or [string]$readCustomHealth.token -ne $customHealthToken -or
        [int]$readCustomHealth.pid -ne $PID -or [string]$readCustomHealth.packageId -ne 'custom-steam-library' -or
        [bool]$readCustomHealth.worker -ne $true -or [bool]$readCustomHealth.workspaceUi -ne $true -or
        [bool]$readCustomHealth.webview2 -ne $true -or [bool]$readCustomHealth.dataRootWritable -ne $true) {
      throw 'simulated CustomSteamLibrary startup health marker validation failed'
    }
    Remove-Item -LiteralPath $customHealthPath -Force -ErrorAction SilentlyContinue

    if ($FailureStage -eq 'main-health') { throw 'simulated YeManCC startup handshake failure' }

    $handshakePath = Join-Path $ScenarioRoot 'update-handshake-test.json'
    $handshakeToken = 'test-handshake-token'
    $handshake = [ordered]@{ phase = 'started'; pid = $PID; token = $handshakeToken }
    Write-Utf8Atomic $handshakePath ($handshake | ConvertTo-Json -Compress)
    $marker = Get-Content -LiteralPath $handshakePath -Raw | ConvertFrom-Json
    if ($marker.phase -ne 'started' -or [int]$marker.pid -ne $PID -or [string]$marker.token -ne $handshakeToken) {
      throw 'simulated startup handshake validation failed'
    }
    Write-Utf8Atomic $statePath ('{"phase":"committed","version":' + (ConvertTo-Json ([string]$manifest.version) -Compress) + ',"pid":' + $PID + '}')
    $updateCommitted = $true
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
      try { Restore-OrdinaryFiles $rollbackFiles $exeDir $pcDir $customDir $supportPath $addedFiles } catch { $rollbackError = 'ordinary rollback: ' + $_.Exception.Message }
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
    CustomDir = $customDir
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
  $layoutManifestPath = Join-Path $packageRoot 'YeManCC\update-manifest.json'
  $hasLayoutManifest = Test-Path -LiteralPath $layoutManifestPath -PathType Leaf
  if ($hasLayoutManifest) {
    $layoutManifest = (Get-Content -LiteralPath $layoutManifestPath -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json
    if ([int]$layoutManifest.schemaVersion -ne 1 -or [string]$layoutManifest.packageId -ne 'yemancc-update' -or [string]$layoutManifest.packageVersion -ne [string]$manifest.version) {
      throw 'update-manifest.json has an invalid schema, packageId or version'
    }
    $declaredRoots = @($layoutManifest.roots | ForEach-Object { [string]$_.source } | Sort-Object -Unique)
    $requiredRoots = @($layoutManifest.requiredRoots | ForEach-Object { [string]$_ } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
    if ($requiredRoots.Count -eq 0) { throw 'update-manifest must declare at least one required root' }
    foreach ($required in $requiredRoots) {
      if ($required -notin $declaredRoots) { throw "required root has no definition: $required" }
    }
  } else {
    $declaredRoots = @('YeManCC', 'PowerControl')
    $requiredRoots = $declaredRoots
  }
  $missingRoots = @($requiredRoots | Where-Object { $_ -notin $topLevel })
  $unexpectedRoots = @($topLevel | Where-Object { $_ -notin $declaredRoots })
  if ($missingRoots.Count -gt 0 -or $unexpectedRoots.Count -gt 0) {
    throw "package roots do not match update-manifest.json; missing: $($missingRoots -join ', '); unexpected: $($unexpectedRoots -join ', '); got: $($topLevel -join ', ')"
  }
  if ($hasLayoutManifest) {
    Assert-Equal ([string]$layoutManifest.rules.fanHost) 'preserve-existing' 'Fan Host update policy'
  }
  if (Test-Path -LiteralPath (Join-Path $packageRoot 'PowerControl\fan-host')) {
    throw 'PowerControl\fan-host unexpectedly entered the update package'
  }
  if (Get-ChildItem -LiteralPath $packageRoot -Recurse -File -Filter 'exclude.txt') {
    throw 'obsolete exclude.txt entered the update package'
  }
  Assert-Equal ((Get-Content -LiteralPath (Join-Path $packageRoot 'YeManCC\version.json') -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json).version ([string]$manifest.version) 'packaged version'
  $requiredPackageItems = @(
    'YeManCC\YeManCC.exe',
    'YeManCC\index.html',
    'YeManCC\YeMan-Support.html',
    'PowerControl\Sleep\system-blacklist.txt',
    'PowerControl\pawnio\YeManTdpCtl.exe',
    'PowerControl\pawnio\_internal'
  )
  if ($hasLayoutManifest) { $requiredPackageItems += 'YeManCC\update-manifest.json' }
  $requiredPackageItems += @(
    'YeManCC\CustomSteamLibrary\CustomSteamLibrary.exe',
    'YeManCC\CustomSteamLibrary\SteamArtworkLab.exe',
    'YeManCC\CustomSteamLibrary\package-manifest.json',
    'YeManCC\CustomSteamLibrary\workspace-ui\index.html'
  )
  foreach ($required in $requiredPackageItems) {
    if (!(Test-Path -LiteralPath (Join-Path $packageRoot $required))) { throw "required package item missing: $required" }
  }

  # v0.0.22 is the compatibility bridge: it updates YeManCC and
  # PowerControl, embeds CustomSteamLibrary under YeManCC, and deliberately
  # does not create a third ZIP root or a legacy sibling path.
  if (!(Test-Path -LiteralPath (Join-Path $packageRoot 'YeManCC\CustomSteamLibrary\package-manifest.json') -PathType Leaf)) {
    $bridgeRoot = Join-Path $testRoot 'bridge-installed'
    $bridgeExe = Join-Path $bridgeRoot 'YeManCC'
    $bridgePowerControl = Join-Path $bridgeRoot 'PowerControl'
    $bridgeCustom = Join-Path $bridgeExe 'CustomSteamLibrary'
    New-Item -ItemType Directory -Path $bridgeExe, $bridgePowerControl, $bridgeCustom -Force | Out-Null
    Copy-TreeChecked (Join-Path $packageRoot 'YeManCC') $bridgeExe
    Copy-TreeChecked (Join-Path $packageRoot 'PowerControl') $bridgePowerControl
    Assert-FileMatch (Join-Path $packageRoot 'YeManCC\CustomSteamLibrary\CustomSteamLibrary.exe') (Join-Path $bridgeCustom 'CustomSteamLibrary.exe') 'bridge nested CustomSteamLibrary entry point'
    Set-Content -LiteralPath (Join-Path $bridgeCustom 'existing-child.marker') -Value 'nested-child-preserved' -Encoding UTF8
    if (!(Test-Path -LiteralPath (Join-Path $bridgeCustom 'existing-child.marker') -PathType Leaf)) {
      throw 'nested CustomSteamLibrary child was not preserved by bridge install'
    }
    if (Test-Path -LiteralPath (Join-Path $bridgeRoot 'CustomSteamLibrary')) {
      throw 'bridge install unexpectedly created a legacy sibling CustomSteamLibrary root'
    }
    $bridgeVersion = (Get-Content -LiteralPath (Join-Path $bridgeExe 'version.json') -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json
    Assert-Equal ([string]$bridgeVersion.version) ([string]$manifest.version) 'bridge installed version'
    Write-Output 'updater install chain self-test: PASS (legacy bootstrap)'
    Write-Output 'bootstrap round: YeManCC and PowerControl updated, nested CustomSteamLibrary preserved, legacy sibling not created'
    return
  }

  # Forward-compatibility probe: a future release may add a declared root
  # without changing the envelope validator or the install transaction.
  $futureLayoutRoot = Join-Path $testRoot 'future-layout'
  Copy-TreeChecked $packageRoot $futureLayoutRoot
  New-Item -ItemType Directory -Path (Join-Path $futureLayoutRoot 'FutureComponent') -Force | Out-Null
  Set-Content -LiteralPath (Join-Path $futureLayoutRoot 'FutureComponent\future.txt') -Value 'future-payload' -Encoding UTF8
  $futureManifestPath = Join-Path $futureLayoutRoot 'YeManCC\update-manifest.json'
  $futureManifest = (Get-Content -LiteralPath $futureManifestPath -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json
  $futureManifest.roots = @($futureManifest.roots) + @([pscustomobject]@{ source = 'FutureComponent'; target = 'FutureComponent'; mode = 'managed-tree' })
  $futureManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $futureManifestPath -Encoding UTF8
  $futureDeclaredRoots = @($futureManifest.roots | ForEach-Object { [string]$_.source } | Sort-Object -Unique)
  $futureActualRoots = @(Get-ChildItem -LiteralPath $futureLayoutRoot -Directory | ForEach-Object Name | Sort-Object -Unique)
  if (Compare-Object $futureDeclaredRoots $futureActualRoots) { throw 'future declared root was not accepted by the manifest layout probe' }
  $futureInstallRoot = Join-Path $testRoot 'future-installed'
  Copy-TreeChecked (Join-Path $futureLayoutRoot 'FutureComponent') (Join-Path $futureInstallRoot 'FutureComponent')
  Assert-Equal (Get-Content -LiteralPath (Join-Path $futureInstallRoot 'FutureComponent\future.txt') -Raw).Trim() 'future-payload' 'future root payload'

  $successRoot = Join-Path $testRoot 'success'
  $successExe = Join-Path $successRoot 'installed\YeManCC'
  $successPc = Join-Path $successRoot 'installed\PowerControl'
  $successCustom = Join-Path $successRoot 'installed\CustomSteamLibrary'
  New-Item -ItemType Directory -Path $successExe, $successPc, $successCustom | Out-Null
  $successBaseline = New-OldInstall $packageRoot $successExe $successPc $successCustom
  Copy-TreeChecked $packageRoot (Join-Path $successRoot 'staging')
  $successResult = Invoke-InstallRound $packageRoot $successRoot ''
  if (!$successResult.Committed) { throw "success round did not commit: $($successResult.FailureMessage)" }
  Assert-Equal ((Get-Content -LiteralPath (Join-Path $successResult.ExeDir 'version.json') -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json).version ([string]$manifest.version) 'success installed version'
  Assert-FileMatch (Join-Path $packageRoot 'YeManCC\index.html') (Join-Path $successResult.ExeDir 'index.html') 'success program files'
  Assert-FileMatch (Join-Path $packageRoot 'YeManCC\YeMan-Support.html') (Join-Path $successResult.ExeDir 'YeMan-Support.html') 'success support page'
  Assert-FileMatch (Join-Path $packageRoot 'PowerControl\Sleep\system-blacklist.txt') (Join-Path $successResult.PcDir 'Sleep\system-blacklist.txt') 'success system blacklist'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $successResult.PcDir 'Sleep\player-blacklist.txt') -Raw).Trim() 'player-owned-rule' 'success player blacklist'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $successResult.PcDir 'Sleep\player-owned.json') -Raw).Trim() 'keep-player-data' 'success player data'
  if (!(Test-Path -LiteralPath (Join-Path $successResult.PcDir 'exclude.txt') -PathType Leaf)) { throw 'existing user exclude.txt was unexpectedly deleted' }
  Assert-Equal (Get-Content -LiteralPath (Join-Path $successResult.PcDir 'fan-host\old-host-marker.txt') -Raw).Trim() 'keep-old-fan-host' 'success existing Fan Host preservation'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $successResult.CustomDir 'data\config\user.json') -Raw).Trim() 'keep-custom-data' 'success CustomSteamLibrary data'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $successResult.CustomDir 'user-owned.txt') -Raw).Trim() 'keep-custom-unknown' 'success CustomSteamLibrary unknown file'
  Assert-FileMatch (Join-Path $packageRoot 'YeManCC\CustomSteamLibrary\CustomSteamLibrary.exe') (Join-Path $successResult.CustomDir 'CustomSteamLibrary.exe') 'success CustomSteamLibrary entry point'
  Assert-FileMatch (Join-Path $packageRoot 'YeManCC\CustomSteamLibrary\SteamArtworkLab.exe') (Join-Path $successResult.CustomDir 'SteamArtworkLab.exe') 'success CustomSteamLibrary worker'
  if (Test-Path -LiteralPath (Join-Path $successResult.PcDir 'pawnio\old-runtime-marker.txt')) { throw 'old PawnIO runtime leaked into successful update' }
  Assert-TreeMatch (Join-Path $packageRoot 'PowerControl\pawnio') (Join-Path $successResult.PcDir 'pawnio') 'success PawnIO runtime'
  if (Test-Path -LiteralPath $successResult.Staging) { throw 'success staging directory was not cleaned' }
  if (Test-Path -LiteralPath $successResult.RollbackRoot) { throw 'success rollback directory was not cleaned' }
  $successProgress = Get-Content -LiteralPath $successResult.ProgressPath -Raw -Encoding UTF8 | ConvertFrom-Json
  Assert-Equal $successProgress.phase 'completed' 'success progress phase'
  $successState = Get-Content -LiteralPath $successResult.StatePath -Raw -Encoding UTF8 | ConvertFrom-Json
  Assert-Equal $successState.phase 'committed' 'success transaction state phase'

  $failureRoot = Join-Path $testRoot 'failure'
  $failureExe = Join-Path $failureRoot 'installed\YeManCC'
  $failurePc = Join-Path $failureRoot 'installed\PowerControl'
  $failureCustom = Join-Path $failureRoot 'installed\CustomSteamLibrary'
  New-Item -ItemType Directory -Path $failureExe, $failurePc, $failureCustom | Out-Null
  $failureBaseline = New-OldInstall $packageRoot $failureExe $failurePc $failureCustom
  Copy-TreeChecked $packageRoot (Join-Path $failureRoot 'staging')
  $oldPawnioSnapshot = Join-Path $failureRoot 'expected-old-pawnio'
  Copy-TreeChecked (Join-Path $failurePc 'pawnio') $oldPawnioSnapshot
  $failureResult = Invoke-InstallRound $packageRoot $failureRoot 'main-health'
  if (!$failureResult.RollbackSucceeded) { throw "failure round did not complete rollback: $($failureResult.FailureMessage)" }
  Assert-Equal ((Get-Content -LiteralPath (Join-Path $failureResult.ExeDir 'version.json') -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json).version '0.0.10' 'rollback installed version'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $failureResult.ExeDir 'index.html') -Raw).Trim() 'old-index' 'rollback program file'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $failureResult.ExeDir 'YeMan-Support.html') -Raw).Trim() 'old-support' 'rollback support page'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $failureResult.PcDir 'Sleep\system-blacklist.txt') -Raw).Trim() 'old-system-rule' 'rollback system blacklist'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $failureResult.PcDir 'Sleep\player-blacklist.txt') -Raw).Trim() 'player-owned-rule' 'rollback player blacklist'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $failureResult.PcDir 'Sleep\player-owned.json') -Raw).Trim() 'keep-player-data' 'rollback player data'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $failureResult.CustomDir 'data\config\user.json') -Raw).Trim() 'keep-custom-data' 'rollback CustomSteamLibrary data'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $failureResult.CustomDir 'user-owned.txt') -Raw).Trim() 'keep-custom-unknown' 'rollback CustomSteamLibrary unknown file'
  Assert-FileMatch (Join-Path $packageRoot 'YeManCC\CustomSteamLibrary\CustomSteamLibrary.exe') (Join-Path $failureResult.CustomDir 'CustomSteamLibrary.exe') 'rollback CustomSteamLibrary entry point'
  Assert-TreeMatch $oldPawnioSnapshot (Join-Path $failureResult.PcDir 'pawnio') 'rollback PawnIO runtime'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $failureResult.PcDir 'fan-host\old-host-marker.txt') -Raw).Trim() 'keep-old-fan-host' 'rollback existing Fan Host preservation'
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

  $customFailureRoot = Join-Path $testRoot 'custom-health-failure'
  $customFailureExe = Join-Path $customFailureRoot 'installed\YeManCC'
  $customFailurePc = Join-Path $customFailureRoot 'installed\PowerControl'
  $customFailureCustom = Join-Path $customFailureRoot 'installed\CustomSteamLibrary'
  New-Item -ItemType Directory -Path $customFailureExe, $customFailurePc, $customFailureCustom | Out-Null
  New-OldInstall $packageRoot $customFailureExe $customFailurePc $customFailureCustom | Out-Null
  Copy-TreeChecked $packageRoot (Join-Path $customFailureRoot 'staging')
  $customFailureResult = Invoke-InstallRound $packageRoot $customFailureRoot 'custom-health'
  if (!$customFailureResult.RollbackSucceeded) { throw "CustomSteamLibrary health failure did not roll back: $($customFailureResult.FailureMessage)" }
  Assert-Equal ((Get-Content -LiteralPath (Join-Path $customFailureResult.ExeDir 'version.json') -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json).version '0.0.10' 'Custom health rollback installed version'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $customFailureResult.CustomDir 'data\config\user.json') -Raw).Trim() 'keep-custom-data' 'Custom health rollback preserved data'
  Assert-Equal (Get-Content -LiteralPath (Join-Path $customFailureResult.CustomDir 'user-owned.txt') -Raw).Trim() 'keep-custom-unknown' 'Custom health rollback preserved unknown file'
  $customFailureState = Get-Content -LiteralPath $customFailureResult.StatePath -Raw -Encoding UTF8 | ConvertFrom-Json
  Assert-Equal $customFailureState.phase 'rolled-back' 'Custom health rollback state phase'

  Write-Output 'updater install chain self-test: PASS'
  Write-Output 'success round: CustomSteamLibrary health marker and YeManCC handshake committed, system blacklist updated, player blacklist preserved, PawnIO replaced'
  Write-Output 'failure round: YeManCC handshake failure rolled back ordinary files and PawnIO, recovery process launched'
  Write-Output 'Custom failure round: CustomSteamLibrary health failure rolled back the complete transaction and preserved data'
} finally {
  if (Test-Path -LiteralPath $testRoot) {
    Remove-Item -LiteralPath $testRoot -Recurse -Force
  }
}
