# Immutable HC delivery trees follow the incoming package, never installed
# originals or a former trimming profile. User configuration lives elsewhere.
function Assert-YmccHcTree([string]$Root) {
    $full=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($full);$seen=0
    while($pending.Count){
        $directory=$pending.Pop();$entry=Get-Item -LiteralPath $directory -Force -ErrorAction Stop
        if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'HC_RUNTIME_REPARSE_POINT'}
        foreach($item in @(Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop)){
            if(++$seen -gt 50000){throw 'HC_RUNTIME_INVENTORY_LIMIT'}
            if(!$item.FullName.StartsWith($full+'\',[StringComparison]::OrdinalIgnoreCase) -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'HC_RUNTIME_PATH_UNSAFE'}
            if($item.PSIsContainer){$pending.Push($item.FullName)}
        }
    }
}
function Sync-YmccHcRuntime([string]$Source,[string]$PowerControl) {
    $manifestPath=Join-Path $Source 'HandheldCompanion.runtime.json'
    if(!(Test-Path -LiteralPath $manifestPath -PathType Leaf) -or (Get-Item -LiteralPath $manifestPath).Length -gt 4194304){throw 'HC_RUNTIME_MANIFEST_MISSING_OR_TOO_LARGE'}
    Assert-YmccHcTree $Source
    $manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json
    $runtimeId=[string]$manifest.runtimeId
    if($manifest.schemaVersion -ne 1 -or $runtimeId -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$' -or $runtimeId -ne (Split-Path -Leaf $Source)){throw 'HC_RUNTIME_ID_INVALID'}
    $managed=@{};$entries=@($manifest.files)
    if(!$entries.Count -or $entries.Count -gt 50000){throw 'HC_RUNTIME_FILE_COUNT_INVALID'}
    foreach($entry in $entries){
        $relative=Normalize-ManifestRelativePath ([string]$entry.path)
        if($relative -ieq 'HandheldCompanion.runtime.json' -or $managed.ContainsKey($relative) -or [string]$entry.sha256 -notmatch '^[0-9a-fA-F]{64}$'){throw 'HC_RUNTIME_ENTRY_INVALID'}
        $file=Join-Path $Source $relative
        if(!(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ine [string]$entry.sha256){throw ('HC_RUNTIME_SOURCE_HASH: '+$relative)}
        $managed[$relative]=$true
    }
    $actual=@(Get-ChildItem -LiteralPath $Source -Recurse -File -Force|ForEach-Object {$_.FullName.Substring($Source.TrimEnd('\').Length+1)}|Where-Object {$_ -ine 'HandheldCompanion.runtime.json'})
    if(Compare-Object @($managed.Keys) $actual){throw 'HC_RUNTIME_SOURCE_INVENTORY_MISMATCH'}
    $pc=[IO.Path]::GetFullPath($PowerControl).TrimEnd('\')
    $runtimeRoot=Join-Path $pc 'handheldcompanion-runtime';$target=Join-Path $runtimeRoot $runtimeId
    foreach($parent in @($pc,$runtimeRoot,$target)){
        if(!( [IO.Path]::GetFullPath($parent).StartsWith($pc+'\',[StringComparison]::OrdinalIgnoreCase)) -and $parent -ine $pc){throw 'HC_RUNTIME_TARGET_ESCAPED'}
        if(Test-Path -LiteralPath $parent){if((Get-Item -LiteralPath $parent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'HC_RUNTIME_TARGET_REPARSE_POINT'}}
    }
    if(Test-Path -LiteralPath $target){Assert-YmccHcTree $target}
    # The target is the dedicated, immutable HC runtime directory. /IS and /IT
    # also replace same-length/same-time corruption; /PURGE removes stale DLLs.
    # This function returns one path; robocopy may emit blank success lines.
    $null=Copy-TreeChecked $Source $target @('/PURGE','/IS','/IT','/IM')
    Assert-YmccHcTree $target
    Assert-TreeMatch $Source $target 'HC incoming exact runtime'
    return $target
}
# Validate the authenticated package's independent shared-directory closure.
# Source authority may also supply the canonical HC-local LHM; it is never
# emitted twice into this directory.
function Get-YmccSharedRuntimePlan([string]$Source,[string]$RuntimeId) {
    $sourceFull=[IO.Path]::GetFullPath($Source).TrimEnd('\')
    if($RuntimeId -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]+$'){throw 'SHARED_RUNTIME_ID_INVALID'}
    Assert-YmccHcTree $sourceFull
    $manifestPath=Join-Path $sourceFull 'manifest.json'
    if(!(Test-Path -LiteralPath $manifestPath -PathType Leaf) -or (Get-Item -LiteralPath $manifestPath).Length -gt 4194304){throw 'SHARED_RUNTIME_MANIFEST_REQUIRED'}
    $manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json
    if($manifest.schemaVersion -ne 1 -or [string]$manifest.packagedDirectory -cne 'PowerControl/ymcc-shared-runtime' -or [string]$manifest.binaryDirectory -cne 'bin' -or @($manifest.files).Count -lt 1 -or @($manifest.files).Count -gt 128){throw 'SHARED_RUNTIME_MANIFEST_INVALID'}
    $records=New-Object 'Collections.Generic.Dictionary[string,object]' ([StringComparer]::OrdinalIgnoreCase)
    function Add-YmccSharedPlanEntry([string]$Relative,[string]$Sha,$Bytes=$null){
        $relativeSafe=Normalize-ManifestRelativePath $Relative
        if($records.ContainsKey($relativeSafe) -or $Sha -notmatch '^[0-9A-Fa-f]{64}$'){throw 'SHARED_RUNTIME_ENTRY_INVALID'}
        $file=Join-Path $sourceFull $relativeSafe
        if(!(Test-Path -LiteralPath $file -PathType Leaf) -or ($null -ne $Bytes -and (Get-Item -LiteralPath $file).Length -ne [int64]$Bytes) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ine $Sha){throw ('SHARED_RUNTIME_SOURCE_HASH: '+$relativeSafe)}
        $records.Add($relativeSafe,$Sha)
    }
    $binaryNames=@{}
    foreach($entry in @($manifest.files)){
        $name=[string]$entry.packagedPath
        if($name -notmatch '^[A-Za-z0-9_.-]+\.dll$' -or $binaryNames.ContainsKey($name) -or [string]$entry.path -cne ('bin/'+$name) -or [string]$entry.sha256 -notmatch '^[0-9A-Fa-f]{64}$' -or [long]$entry.bytes -le 0){throw 'SHARED_RUNTIME_BINARY_INVALID'}
        $binaryNames[$name]=$true
        if($entry.PSObject.Properties['packagedDirectory'] -and [string]$entry.packagedDirectory -cne [string]$manifest.packagedDirectory){
            if($name -cne 'LibreHardwareMonitorLib.dll' -or [string]$entry.packagedDirectory -cne ('PowerControl/handheldcompanion-runtime/'+$RuntimeId)){throw 'SHARED_RUNTIME_DISTRIBUTION_INVALID'}
            $canonical=Join-Path (Split-Path -Parent $sourceFull) ('handheldcompanion-runtime/'+$RuntimeId+'/'+$name)
            if(!(Test-Path -LiteralPath $canonical -PathType Leaf) -or (Get-Item -LiteralPath $canonical).Length -ne [long]$entry.bytes -or (Get-FileHash -LiteralPath $canonical -Algorithm SHA256).Hash -ine [string]$entry.sha256){throw 'SHARED_RUNTIME_CANONICAL_LHM_INVALID'}
            continue
        }
        Add-YmccSharedPlanEntry $name ([string]$entry.sha256) $entry.bytes
    }
    $metadata=@($manifest.licenses)+@($manifest.provenanceFiles)
    if($metadata.Count -gt 256){throw 'SHARED_RUNTIME_METADATA_LIMIT'}
    foreach($entry in $metadata){
        if([string]$entry.path -notmatch '^(licenses|provenance)/[^/\\:]+$'){throw 'SHARED_RUNTIME_METADATA_INVALID'}
        Add-YmccSharedPlanEntry ([string]$entry.path) ([string]$entry.sha256)
    }
    foreach($name in @('README.md','manifest.json')){
        $file=Join-Path $sourceFull $name
        if(!(Test-Path -LiteralPath $file -PathType Leaf)){throw 'SHARED_RUNTIME_METADATA_REQUIRED'}
        $records.Add($name,(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash)
    }
    $actual=@(Get-ChildItem -LiteralPath $sourceFull -Recurse -File -Force|ForEach-Object{$_.FullName.Substring($sourceFull.Length+1)})
    if(Compare-Object @($records.Keys) $actual){throw 'SHARED_RUNTIME_SOURCE_INVENTORY_MISMATCH'}
    return [pscustomobject]@{source=$sourceFull;runtimeId=$RuntimeId;files=$records}
}
function Sync-YmccSharedRuntime([string]$Source,[string]$PowerControl,[string]$RuntimeId){
    $plan=Get-YmccSharedRuntimePlan $Source $RuntimeId
    $pc=[IO.Path]::GetFullPath($PowerControl).TrimEnd('\');$target=Join-Path $pc 'ymcc-shared-runtime'
    foreach($parent in @($pc,$target)){
        if(Test-Path -LiteralPath $parent){if((Get-Item -LiteralPath $parent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'SHARED_RUNTIME_TARGET_REPARSE_POINT'}}
    }
    if(Test-Path -LiteralPath $target){Assert-YmccHcTree $target}
    $null=Copy-TreeChecked $plan.source $target @('/PURGE','/IS','/IT','/IM')
    Assert-YmccHcTree $target
    Assert-TreeMatch $plan.source $target 'Incoming shared dependency closure'
    return $target
}
# This stays inside the existing updater/module lease. No SDK/device action.
# The existing installer validates files and writes a deployment receipt. Run
# that same script in an owned child with a deadline; never wait on pipe EOF.
function Invoke-YmccFanPayloadInstaller([string]$Script,[string]$PayloadDirectory,[ValidateRange(100,30000)][int]$TimeoutMilliseconds=20000) {
    $target=[IO.Path]::GetFullPath($PayloadDirectory).TrimEnd('\')
    $file=[IO.Path]::GetFullPath($Script)
    if($file -ine (Join-Path $target 'install-fan-host-payload.ps1') -or !(Test-Path -LiteralPath $file -PathType Leaf)){throw 'FAN_HOST_INSTALLER_PATH_INVALID'}
    Assert-YmccHcTree $target
    # A failed termination must not launch an overlapping validation/receipt writer.
    if($script:YmccFanPayloadInstallerWorker){
        if(!$script:YmccFanPayloadInstallerWorker.HasExited){throw 'FAN_HOST_INSTALLER_PREVIOUS_WORKER_PENDING'}
        $script:YmccFanPayloadInstallerWorker.Dispose();$script:YmccFanPayloadInstallerWorker=$null
    }
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=Join-Path ([Environment]::GetFolderPath('System')) 'WindowsPowerShell/v1.0/powershell.exe'
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WorkingDirectory=$target
    # Windows paths cannot contain a quote. There is no command interpreter.
    $start.Arguments='-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$file+'" -PayloadDirectory "'+$target+'"'
    $worker=New-Object Diagnostics.Process;$worker.StartInfo=$start
    try {
        if(!$worker.Start()){throw 'FAN_HOST_INSTALLER_START_FAILED'}
        $script:YmccFanPayloadInstallerWorker=$worker
        if(!$worker.WaitForExit($TimeoutMilliseconds)){
            try{if(!$worker.HasExited){$worker.Kill()}}catch{}
            $stopped=$worker.WaitForExit(3000)
            throw ('FAN_HOST_INSTALLER_TIMEOUT: budgetMs='+$TimeoutMilliseconds+'; ownedWorkerStopped='+$stopped)
        }
        if($worker.ExitCode -ne 0){throw ('Fan Host payload installation failed: '+$worker.ExitCode)}
    } finally {
        if(!$script:YmccFanPayloadInstallerWorker -or $worker.HasExited){
            $worker.Dispose();$script:YmccFanPayloadInstallerWorker=$null
        }
    }
}

function Sync-YmccImmutableFanPayload([string]$RuntimeSource,[string]$HostSource,[string]$PowerControl){
    Assert-YmccHcTree $RuntimeSource;Assert-YmccHcTree $HostSource
    $runtimeManifest=Get-Content -LiteralPath (Join-Path $RuntimeSource 'HandheldCompanion.runtime.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $runtimeId=[string]$runtimeManifest.runtimeId
    $hostManifestPath=Join-Path $HostSource 'YeManFanHost.payload.json'
    if(!(Test-Path -LiteralPath $hostManifestPath -PathType Leaf) -or (Get-Item -LiteralPath $hostManifestPath).Length -gt 4194304){throw 'IMMUTABLE_HOST_MANIFEST_REQUIRED'}
    $hostPayload=Get-Content -LiteralPath $hostManifestPath -Raw -Encoding UTF8|ConvertFrom-Json
    if($hostPayload.schemaVersion -ne 2 -or [string]$hostPayload.runtimeId -cne $runtimeId -or @($hostPayload.files).Count -lt 1 -or @($hostPayload.files).Count -gt 512){throw 'IMMUTABLE_HOST_SOURCE_IDENTITY_INVALID'}
    $runtimeReference=([string]$hostPayload.runtimeManifest).Replace([IO.Path]::DirectorySeparatorChar,[char]'/')
    if($runtimeReference -cne ('../handheldcompanion-runtime/'+$runtimeId+'/HandheldCompanion.runtime.json')){throw 'IMMUTABLE_HOST_RUNTIME_BINDING_INVALID'}
    $names=@{}
    foreach($entry in @($hostPayload.files)){
        $name=[string]$entry.path
        if($name -notmatch '^[^/\\:]+$' -or $name -in @('.','..','YeManFanHost.payload.json') -or $names.ContainsKey($name) -or [string]$entry.sha256 -notmatch '^[0-9A-Fa-f]{64}$'){throw 'IMMUTABLE_HOST_ENTRY_INVALID'}
        $file=Join-Path $HostSource $name
        if(!(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ine [string]$entry.sha256){throw 'IMMUTABLE_HOST_SOURCE_HASH'}
        $names[$name]=$true
    }
    $actual=@(Get-ChildItem -LiteralPath $HostSource -File -Recurse -Force|ForEach-Object{$_.FullName.Substring($HostSource.TrimEnd('\').Length+1)}|Where-Object{$_ -ine 'YeManFanHost.payload.json'})
    if(Compare-Object @($names.Keys) $actual){throw 'IMMUTABLE_HOST_SOURCE_INVENTORY_MISMATCH'}
    $sharedSource=Join-Path (Split-Path -Parent $RuntimeSource | Split-Path -Parent) 'ymcc-shared-runtime'
    $hasShared=Test-Path -LiteralPath $sharedSource -PathType Container
    if($hasShared){$null=Get-YmccSharedRuntimePlan $sharedSource $runtimeId}
    elseif($runtimeManifest.PSObject.Properties['sharedDependencies']){throw 'IMMUTABLE_SHARED_SOURCE_REQUIRED'}
    $pc=[IO.Path]::GetFullPath($PowerControl).TrimEnd('\');$fanHostTarget=Join-Path $pc 'fan-host-v2'
    foreach($root in @($pc,$fanHostTarget)){
        if(Test-Path -LiteralPath $root){
            if(!(Test-Path -LiteralPath $root -PathType Container) -or ((Get-Item -LiteralPath $root -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'IMMUTABLE_HOST_TARGET_UNSAFE'}
        }
    }
    if(Test-Path -LiteralPath $fanHostTarget){Assert-YmccHcTree $fanHostTarget}
    $installedRuntime=Sync-YmccHcRuntime $RuntimeSource $pc
    if($hasShared){$null=Sync-YmccSharedRuntime $sharedSource $pc $runtimeId}
    $null=Copy-TreeChecked $HostSource $fanHostTarget @('/PURGE','/IS','/IT','/IM')
    Assert-YmccHcTree $fanHostTarget
    Assert-TreeMatch $HostSource $fanHostTarget 'Fan Host incoming whole-directory copy'
    $fanHostAclScript=Join-Path $fanHostTarget 'install-fan-host-payload.ps1'
    if(!(Test-Path -LiteralPath $fanHostAclScript -PathType Leaf)){throw 'Fan Host payload installer missing after update copy'}
    Invoke-YmccFanPayloadInstaller $fanHostAclScript $fanHostTarget
    return [pscustomobject]@{runtimeTarget=$installedRuntime;runtimeId=$runtimeId;hostTarget=$fanHostTarget}
}

function Remove-YmccSupersededHcRuntimes([string]$PowerControl,[string]$CurrentId,[ValidateRange(1,64)][int]$Maximum=64) {
    $pc=[IO.Path]::GetFullPath($PowerControl).TrimEnd('\');$root=Join-Path $pc 'handheldcompanion-runtime'
    if(!(Test-Path -LiteralPath $root -PathType Container)){return}
    if((Get-Item -LiteralPath $root -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'HC_RUNTIME_ROOT_REPARSE_POINT'}
    $retiredCount=0
    foreach($directory in @(Get-ChildItem -LiteralPath $root -Directory -Force)){
        if($directory.Name -ieq $CurrentId -or $directory.Name -notmatch '^(YMCC-|HC-)[A-Za-z0-9_.-]+$'){continue}
        $manifestPath=Join-Path $directory.FullName 'HandheldCompanion.runtime.json'
        if(!(Test-Path -LiteralPath $manifestPath -PathType Leaf) -or (Get-Item -LiteralPath $manifestPath).Length -gt 4194304){continue}
        Assert-YmccHcTree $directory.FullName
        try{$manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json}catch{continue}
        if($manifest.schemaVersion -ne 1 -or [string]$manifest.runtimeId -ine $directory.Name){continue}
        $full=[IO.Path]::GetFullPath($directory.FullName)
        if(!( [IO.Path]::GetDirectoryName($full) -ieq $root)){throw 'HC_RUNTIME_CLEANUP_ESCAPED'}
        if($retiredCount -ge $Maximum){throw 'HC_RUNTIME_CLEANUP_BUDGET_EXHAUSTED'};++$retiredCount
        # Old managed runtime IDs are no longer executable authorities after
        # commit. Clear checked files and empty directories without following links.
        # Preserve the small ownership manifest if a later file/directory is
        # locked. Retrying the same canonical cleanup can then rediscover it.
        $ownershipBytes=[IO.File]::ReadAllBytes($manifestPath)
        try {
        foreach($file in @(Get-ChildItem -LiteralPath $full -Recurse -File -Force|Sort-Object {$_.FullName -ieq $manifestPath})){Remove-Item -LiteralPath $file.FullName -Force -ErrorAction Stop}
        foreach($child in @(Get-ChildItem -LiteralPath $full -Recurse -Directory -Force|Sort-Object {$_.FullName.Length} -Descending)){[IO.Directory]::Delete($child.FullName,$false)}
        [IO.Directory]::Delete($full,$false)
        } catch {
            if([IO.Directory]::Exists($full) -and ![IO.File]::Exists($manifestPath)){
                Assert-YmccHcTree $full
                [IO.File]::WriteAllBytes($manifestPath,$ownershipBytes)
            }
            throw
        }
    }
}
