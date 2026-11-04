# Developer-only, explicit local transaction helpers. Dot-sourcing has no actions.
# The caller owns canonical ZIP/source admission, export + ModuleMaintenance locks,
# stopped exact target paths, ordinary-tree snapshots, journal, rollback and commit.
# Equal versions are permitted here; this is never public interrupted recovery.
# No source-index/build inclusion is implied by this development tool.

function ConvertTo-YmccLocalFanPath([string]$Path) {
    $relative=$Path.Replace('\','/')
    if($relative.Length -gt 1024 -or !$relative -or $relative.StartsWith('/') -or $relative -match '[<>:"|?*\x00-\x1f]' -or $relative.Contains('//')){throw "LOCAL_FAN_UNSAFE_PATH: $Path"}
    foreach($part in $relative.Split('/')){
        if(!$part -or $part -in @('.','..') -or $part -match '[. ]$' -or $part -match '^(?i:CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)'){throw "LOCAL_FAN_UNSAFE_PATH: $Path"}
    }
    return $relative
}
function Assert-YmccLocalFanPath([string]$Path,[string]$Kind='Any') {
    $full=[IO.Path]::GetFullPath($Path).TrimEnd('\')
    $root=[IO.Path]::GetPathRoot($full)
    if(!$root -or $full.Length -le $root.Length){throw 'LOCAL_FAN_ROOT_PATH_FORBIDDEN'}
    $null=ConvertTo-YmccLocalFanPath $full.Substring($root.Length)
    # Check ancestors as well as the leaf; no traversal through a junction.
    $cursor=$full
    while($cursor){
        if(Test-Path -LiteralPath $cursor){
            $item=Get-Item -LiteralPath $cursor -Force -ErrorAction Stop
            if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "LOCAL_FAN_REPARSE_POINT: $cursor"}
            if($cursor -ine $full -and !$item.PSIsContainer){throw 'LOCAL_FAN_PARENT_NOT_DIRECTORY'}
        }
        $next=[IO.Path]::GetDirectoryName($cursor)
        if($next -eq $cursor){break};$cursor=$next
    }
    if($Kind -eq 'Directory' -and !(Test-Path -LiteralPath $full -PathType Container)){throw "LOCAL_FAN_DIRECTORY_REQUIRED: $full"}
    if($Kind -eq 'File' -and !(Test-Path -LiteralPath $full -PathType Leaf)){throw "LOCAL_FAN_FILE_REQUIRED: $full"}
    return $full
}
function Get-YmccLocalFanStreamHash([IO.Stream]$Stream,[long]$Length) {
    if($Length -lt 0 -or $Length -gt 4GB){throw 'LOCAL_FAN_HASH_BYTE_LIMIT'}
    $sha=[Security.Cryptography.SHA256]::Create();$buffer=New-Object byte[] 65536;$seen=0L
    try{
        while($seen -lt $Length){
            $read=$Stream.Read($buffer,0,[int][Math]::Min($buffer.Length,$Length-$seen))
            if(!$read){throw 'LOCAL_FAN_STREAM_LENGTH_MISMATCH'}
            $null=$sha.TransformBlock($buffer,0,$read,$buffer,0);$seen+=$read
        }
        if($Stream.ReadByte() -ne -1){throw 'LOCAL_FAN_STREAM_LENGTH_MISMATCH'}
        $null=$sha.TransformFinalBlock($buffer,0,0)
        return ([BitConverter]::ToString($sha.Hash)).Replace('-','').ToLowerInvariant()
    }finally{$sha.Dispose()}
}
function Get-YmccLocalFanFileRow([string]$File,[string]$Relative) {
    $full=Assert-YmccLocalFanPath $File 'File'
    $stream=[IO.File]::Open($full,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try{return [pscustomobject]@{path=$Relative;bytes=[long]$stream.Length;sha256=(Get-YmccLocalFanStreamHash $stream $stream.Length)}}finally{$stream.Dispose()}
}
function Get-YmccLocalFanTree([string]$Root) {
    $full=Assert-YmccLocalFanPath $Root 'Directory'
    $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($full)
    $rows=[Collections.Generic.List[object]]::new();$names=@{};$seen=0;$bytes=0L
    while($pending.Count){
        $directory=$pending.Pop();$null=Assert-YmccLocalFanPath $directory 'Directory'
        foreach($item in @(Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop)){
            if(++$seen -gt 50000){throw 'LOCAL_FAN_TREE_ENTRY_LIMIT'}
            if(!$item.FullName.StartsWith($full+'\',[StringComparison]::OrdinalIgnoreCase) -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'LOCAL_FAN_TREE_PATH_UNSAFE'}
            $relative=ConvertTo-YmccLocalFanPath $item.FullName.Substring($full.Length+1)
            if($names.ContainsKey($relative)){throw 'LOCAL_FAN_TREE_DUPLICATE'};$names[$relative]=$true
            if($item.PSIsContainer){$pending.Push($item.FullName);continue}
            $bytes+=$item.Length
            if($item.Length -gt 4GB -or $bytes -gt 16GB){throw 'LOCAL_FAN_TREE_BYTE_LIMIT'}
            $rows.Add((Get-YmccLocalFanFileRow $item.FullName $relative))
        }
    }
    return $rows.ToArray()
}
function Read-YmccLocalFanJson([string]$Path) {
    $full=Assert-YmccLocalFanPath $Path 'File'
    if((Get-Item -LiteralPath $full -Force).Length -gt 16MB){throw 'LOCAL_FAN_JSON_SIZE_LIMIT'}
    return ([IO.File]::ReadAllText($full,[Text.UTF8Encoding]::new($false,$true)).TrimStart([char]0xFEFF)|ConvertFrom-Json -ErrorAction Stop)
}
function Get-YmccLocalFanIdentityIndex($Identity) {
    if($Identity.schema -ne 1 -or [string]$Identity.packageId -cne 'YeManCC' -or [string]$Identity.buildId -notmatch '^[a-fA-F0-9]{64}$' -or [string]$Identity.sourceTreeSha256 -notmatch '^[a-fA-F0-9]{64}$' -or [string]::IsNullOrWhiteSpace([string]$Identity.version) -or ([string]$Identity.version).Length -gt 128){throw 'LOCAL_FAN_EXPORT_IDENTITY_INVALID'}
    $rows=@($Identity.payload)
    if(!$rows.Count -or $rows.Count -gt 50000 -or $Identity.payloadFileCount -ne $rows.Count){throw 'LOCAL_FAN_EXPORT_IDENTITY_COUNT_INVALID'}
    $index=@{};$total=0L
    foreach($row in $rows){
        $path=ConvertTo-YmccLocalFanPath ([string]$row.path)
        if($path.Split('/')[0] -cnotin @('YeManCC','PowerControl') -or !$path.Contains('/') -or $path -ieq 'YeManCC/export-identity.json' -or $index.ContainsKey($path) -or [string]$row.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or ([string]$row.bytes) -notmatch '^(0|[1-9][0-9]{0,10})$' -or [long]$row.bytes -lt 0 -or [long]$row.bytes -gt 4GB){throw "LOCAL_FAN_EXPORT_ENTRY_INVALID: $path"}
        $total+=[long]$row.bytes;if($total -gt 16GB){throw 'LOCAL_FAN_EXPORT_BYTE_LIMIT'}
        $index[$path]=[pscustomobject]@{path=$path;bytes=[long]$row.bytes;sha256=([string]$row.sha256).ToLowerInvariant()}
    }
    foreach($required in @('YeManCC/YeManCC.exe','YeManCC/update-manifest.json','PowerControl/fan-host-v2/YeManFanHost.payload.json')){
        if(!$index.ContainsKey($required)){throw "LOCAL_FAN_CANONICAL_ENTRY_REQUIRED: $required"}
    }
    return ,$index
}
function Assert-YmccLocalFanRows($Expected,$Actual,[string]$Label) {
    $index=@{};foreach($row in @($Actual)){
        if($index.ContainsKey([string]$row.path)){throw "LOCAL_FAN_DUPLICATE: $Label"};$index[[string]$row.path]=$row
    }
    if(@($Expected).Count -ne $index.Count){throw "LOCAL_FAN_INVENTORY_MISMATCH: $Label"}
    foreach($row in @($Expected)){
        $path=[string]$row.path
        if(!$index.ContainsKey($path) -or $index[$path].bytes -ne $row.bytes -or $index[$path].sha256 -ine $row.sha256){throw "LOCAL_FAN_HASH_MISMATCH: $Label : $path"}
    }
}
# One manifest/inventory validator serves both the pinned old archive and stage.
function Get-YmccLocalFanTuple($Index,$Documents) {
    $hostPrefix='PowerControl/fan-host-v2/'
    $hostName=$hostPrefix+'YeManFanHost.payload.json';$hostPayload=$Documents[$hostName]
    $id=[string]$hostPayload.runtimeId
    if($hostPayload.schemaVersion -ne 2 -or $id -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$'){throw 'LOCAL_FAN_HOST_IDENTITY_INVALID'}
    $runtimePrefix='PowerControl/handheldcompanion-runtime/'+$id+'/'
    $runtimeName=$runtimePrefix+'HandheldCompanion.runtime.json';$runtime=$Documents[$runtimeName]
    $sharedPrefix='PowerControl/ymcc-shared-runtime/';$sharedName=$sharedPrefix+'manifest.json';$shared=$Documents[$sharedName]
    if((([string]$hostPayload.runtimeManifest).Replace('\','/')) -cne ('../handheldcompanion-runtime/'+$id+'/HandheldCompanion.runtime.json') -or $runtime.schemaVersion -ne 1 -or [string]$runtime.runtimeId -cne $id){throw 'LOCAL_FAN_RUNTIME_BINDING_INVALID'}
    foreach($spec in @(@{prefix=$hostPrefix;name='YeManFanHost.payload.json';files=@($hostPayload.files);limit=512},@{prefix=$runtimePrefix;name='HandheldCompanion.runtime.json';files=@($runtime.files);limit=50000})){
        if(!$spec.files.Count -or $spec.files.Count -gt $spec.limit){throw 'LOCAL_FAN_MANIFEST_COUNT_INVALID'}
        $expected=@{};$expected[$spec.name]=$true
        foreach($file in $spec.files){
            $relative=ConvertTo-YmccLocalFanPath ([string]$file.path)
            if(($spec.prefix -eq $hostPrefix -and $relative.Contains('/')) -or $expected.ContainsKey($relative) -or [string]$file.sha256 -notmatch '^[a-fA-F0-9]{64}$'){throw 'LOCAL_FAN_MANIFEST_ENTRY_INVALID'}
            $expected[$relative]=$true;$path=$spec.prefix+$relative
            if(!$Index.ContainsKey($path) -or $Index[$path].sha256 -ine [string]$file.sha256){throw "LOCAL_FAN_MANIFEST_HASH_MISMATCH: $path"}
        }
        $actual=@($Index.Keys|Where-Object{$_.StartsWith($spec.prefix,[StringComparison]::OrdinalIgnoreCase)})
        if($actual.Count -ne $expected.Count -or !$Index.ContainsKey($spec.prefix+$spec.name)){throw 'LOCAL_FAN_MANIFEST_INVENTORY_MISMATCH'}
        foreach($path in $actual){if(!$expected.ContainsKey($path.Substring($spec.prefix.Length))){throw 'LOCAL_FAN_MANIFEST_UNLISTED_FILE'}}
    }
    if(!$Index.ContainsKey($hostPrefix+'install-fan-host-payload.ps1') -or !$Index.ContainsKey($hostPrefix+'YeManFanHost.exe')){throw 'LOCAL_FAN_HOST_INSTALLER_OR_EXE_MISSING'}
    # Mirror the production shared plan, including canonical HC-local LHM.
    if($shared.schemaVersion -ne 1 -or [string]$shared.packagedDirectory -cne 'PowerControl/ymcc-shared-runtime' -or [string]$shared.binaryDirectory -cne 'bin' -or @($shared.files).Count -lt 1 -or @($shared.files).Count -gt 128){throw 'LOCAL_FAN_SHARED_MANIFEST_INVALID'}
    $expected=@{'manifest.json'=$true;'README.md'=$true};$binaryNames=@{}
    foreach($file in @($shared.files)){
        $name=ConvertTo-YmccLocalFanPath ([string]$file.packagedPath)
        if($name -notmatch '^[A-Za-z0-9_.-]+\.dll$' -or $binaryNames.ContainsKey($name) -or [string]$file.path -cne ('bin/'+$name) -or [string]$file.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or [long]$file.bytes -le 0){throw 'LOCAL_FAN_SHARED_ENTRY_INVALID'}
        $binaryNames[$name]=$true;$prefix=$sharedPrefix
        if($file.PSObject.Properties['packagedDirectory'] -and [string]$file.packagedDirectory -cne 'PowerControl/ymcc-shared-runtime'){
            if($name -cne 'LibreHardwareMonitorLib.dll' -or [string]$file.packagedDirectory -cne $runtimePrefix.TrimEnd('/')){throw 'LOCAL_FAN_SHARED_DISTRIBUTION_INVALID'}
            $prefix=$runtimePrefix
        }else{$expected[$name]=$true}
        $path=$prefix+$name
        if(!$Index.ContainsKey($path) -or $Index[$path].sha256 -ine [string]$file.sha256 -or $Index[$path].bytes -ne [long]$file.bytes){throw "LOCAL_FAN_SHARED_HASH_MISMATCH: $path"}
    }
    $metadata=@($shared.licenses)+@($shared.provenanceFiles)
    if($metadata.Count -gt 256){throw 'LOCAL_FAN_SHARED_METADATA_LIMIT'}
    foreach($file in $metadata){
        $relative=ConvertTo-YmccLocalFanPath ([string]$file.path)
        if($relative -notmatch '^(licenses|provenance)/[^/]+$' -or $expected.ContainsKey($relative) -or [string]$file.sha256 -notmatch '^[a-fA-F0-9]{64}$'){throw 'LOCAL_FAN_SHARED_METADATA_INVALID'}
        $expected[$relative]=$true;$path=$sharedPrefix+$relative
        if(!$Index.ContainsKey($path) -or $Index[$path].sha256 -ine [string]$file.sha256){throw 'LOCAL_FAN_SHARED_METADATA_HASH_MISMATCH'}
    }
    $sharedActual=@($Index.Keys|Where-Object{$_.StartsWith($sharedPrefix,[StringComparison]::OrdinalIgnoreCase)})
    if($sharedActual.Count -ne $expected.Count){throw 'LOCAL_FAN_SHARED_INVENTORY_MISMATCH'}
    foreach($name in $expected.Keys){if(!$Index.ContainsKey($sharedPrefix+$name)){throw 'LOCAL_FAN_SHARED_FILE_REQUIRED'}}
    $dependencies=$runtime.sharedDependencies
    if(!$dependencies -or [string]$dependencies.directory -cne $sharedPrefix.TrimEnd('/') -or [string]$dependencies.manifestSha256 -ine $Index[$sharedName].sha256 -or @($dependencies.files).Count -lt 1 -or @($dependencies.files).Count -gt 128){throw 'LOCAL_FAN_RUNTIME_SHARED_BINDING_INVALID'}
    $dependencyNames=@{}
    foreach($file in @($dependencies.files)){
        $name=ConvertTo-YmccLocalFanPath ([string]$file.path);$path=$sharedPrefix+$name
        if($name.Contains('/') -or $dependencyNames.ContainsKey($name) -or !$binaryNames.ContainsKey($name) -or !$Index.ContainsKey($path) -or [string]$file.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or $Index[$path].sha256 -ine [string]$file.sha256 -or $Index[$path].bytes -ne [long]$file.bytes){throw 'LOCAL_FAN_RUNTIME_SHARED_FILE_INVALID'}
        $dependencyNames[$name]=$true
    }
    $layout=$Documents['YeManCC/update-manifest.json']
    if($layout){
        $binding=$layout.rules.fanHostPayload
        $roots=@($layout.roots|ForEach-Object{[string]$_.source}|Sort-Object -Unique)
        if($layout.schemaVersion -ne 1 -or [string]$layout.packageId -cne 'yemancc-update' -or (Compare-Object @('PowerControl','YeManCC') $roots) -or [string]$layout.rules.fanHost -cne 'preserve-existing' -or [string]$layout.rules.fanHostV2 -cne 'replace' -or [string]$binding.directory -cne 'fan-host-v2' -or $binding.schemaVersion -ne 2 -or $binding.fileCount -ne @($hostPayload.files).Count -or [string]$binding.manifestSha256 -ine $Index[$hostName].sha256 -or (([string]$binding.runtimeDirectory).Replace('\','/')) -cne ('handheldcompanion-runtime/'+$id) -or [string]$binding.runtimeManifestSha256 -ine $Index[$runtimeName].sha256){throw 'LOCAL_FAN_LAYOUT_BINDING_INVALID'}
    }
    $prefixes=@($hostPrefix.TrimEnd('/'),$runtimePrefix.TrimEnd('/'),$sharedPrefix.TrimEnd('/'))
    $entries=@($Index.Values|Where-Object{ $path=$_.path; @($prefixes|Where-Object{$path.StartsWith($_+'/',[StringComparison]::OrdinalIgnoreCase)}).Count -gt 0 }|Sort-Object path)
    return [pscustomobject]@{runtimeId=$id;hostManifestSha256=$Index[$hostName].sha256;runtimeManifestSha256=$Index[$runtimeName].sha256;sharedManifestSha256=$Index[$sharedName].sha256;immutablePrefixes=$prefixes;entries=$entries}
}

function Read-YmccPinnedLocalArchive {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$Archive,[Parameter(Mandatory=$true)][string]$Sha256)
    if($Sha256 -notmatch '^[a-fA-F0-9]{64}$'){throw 'LOCAL_FAN_EXPLICIT_ARCHIVE_SHA256_REQUIRED'}
    $full=Assert-YmccLocalFanPath $Archive 'File'
    if([IO.Path]::GetFileName($full) -cne 'YeManCC.zip'){throw 'LOCAL_FAN_CANONICAL_ARCHIVE_NAME_REQUIRED'}
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $stream=[IO.File]::Open($full,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read);$zip=$null
    try{
        if($stream.Length -gt 4GB){throw 'LOCAL_FAN_ARCHIVE_BYTE_LIMIT'}
        $actualHash=Get-YmccLocalFanStreamHash $stream $stream.Length
        if($actualHash -ine $Sha256){throw 'LOCAL_FAN_ARCHIVE_PIN_MISMATCH'}
        $stream.Position=0;$zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Read,$true)
        if($zip.Entries.Count -gt 50000){throw 'LOCAL_FAN_ARCHIVE_ENTRY_LIMIT'}
        $index=@{};$nodes=@{};$documents=@{};$populatedDirectories=@{};$total=0L
        foreach($entry in $zip.Entries){
            $raw=$entry.FullName.Replace('\','/');$directory=$raw.EndsWith('/')
            $path=ConvertTo-YmccLocalFanPath $(if($directory){$raw.Substring(0,$raw.Length-1)}else{$raw})
            if($path.Split('/')[0] -cnotin @('YeManCC','PowerControl') -or (!$directory -and !$path.Contains('/')) -or $nodes.ContainsKey($path)){throw "LOCAL_FAN_ARCHIVE_ROOT_OR_DUPLICATE: $path"}
            if($path -imatch '^PowerControl/fan-host(?:/|$)' -or $path -imatch '(^|/)fan-host-quarantine(/|$)'){throw 'LOCAL_FAN_ARCHIVE_LEGACY_PAYLOAD_FORBIDDEN'}
            $attributes=([long]$entry.ExternalAttributes -band 0xFFFFFFFFL);$type=($attributes -shr 16) -band 0xF000
            if(($attributes -band 0x400) -or $type -notin @(0,0x8000,0x4000) -or ($type -eq 0x4000 -and !$directory) -or ($type -eq 0x8000 -and $directory) -or (($attributes -band 0x10) -and !$directory)){throw 'LOCAL_FAN_ARCHIVE_LINK_OR_TYPE_UNSAFE'}
            $nodes[$path]=$directory
            if($directory){if($entry.Length -ne 0){throw 'LOCAL_FAN_ARCHIVE_DIRECTORY_DATA'};continue}
            $total+=$entry.Length
            if($entry.Length -gt 4GB -or $total -gt 16GB){throw 'LOCAL_FAN_ARCHIVE_EXPANDED_BYTE_LIMIT'}
            $entryStream=$entry.Open()
            try{$hash=Get-YmccLocalFanStreamHash $entryStream $entry.Length}finally{$entryStream.Dispose()}
            $index[$path]=[pscustomobject]@{path=$path;bytes=[long]$entry.Length;sha256=$hash}
            $parent=$path
            while($parent.Contains('/')){$parent=$parent.Substring(0,$parent.LastIndexOf('/'));$populatedDirectories[$parent]=$true}
            if($path -ieq 'YeManCC/export-identity.json' -or $path -ieq 'YeManCC/update-manifest.json' -or $path -imatch '^PowerControl/(fan-host-v2/YeManFanHost\.payload\.json|handheldcompanion-runtime/[^/]+/HandheldCompanion\.runtime\.json|ymcc-shared-runtime/manifest\.json)$'){
                if($entry.Length -gt 16MB){throw 'LOCAL_FAN_ARCHIVE_JSON_SIZE_LIMIT'}
                $reader=[IO.StreamReader]::new($entry.Open(),[Text.UTF8Encoding]::new($false,$true),$true)
                try{$documents[$path]=$reader.ReadToEnd().TrimStart([char]0xFEFF)|ConvertFrom-Json -ErrorAction Stop}finally{$reader.Dispose()}
            }
        }
        # File/directory collisions include implicit parent directories.
        foreach($path in $nodes.Keys){
            $parent=$path
            while($parent.Contains('/')){
                $parent=$parent.Substring(0,$parent.LastIndexOf('/'))
                if($nodes.ContainsKey($parent) -and !$nodes[$parent]){throw 'LOCAL_FAN_ARCHIVE_FILE_PARENT_COLLISION'}
            }
            if($nodes[$path] -and !$populatedDirectories.ContainsKey($path)){throw 'LOCAL_FAN_ARCHIVE_UNDECLARED_EMPTY_DIRECTORY'}
        }
        $identity=$documents['YeManCC/export-identity.json'];$expected=Get-YmccLocalFanIdentityIndex $identity
        if(!$index.ContainsKey('YeManCC/export-identity.json')){throw 'LOCAL_FAN_ARCHIVE_EXPORT_IDENTITY_REQUIRED'}
        Assert-YmccLocalFanRows @($expected.Values) @($index.Values|Where-Object{$_.path -ine 'YeManCC/export-identity.json'}) 'pinned canonical archive'
        if([string]$documents['YeManCC/update-manifest.json'].packageVersion -cne [string]$identity.version){throw 'LOCAL_FAN_ARCHIVE_VERSION_BINDING_INVALID'}
        $tuple=Get-YmccLocalFanTuple $index $documents
        return [pscustomobject]@{archive=$full;sha256=$actualHash;identity=$identity;entries=@($index.Values|Sort-Object path);tuple=$tuple}
    }finally{if($zip){$zip.Dispose()};$stream.Dispose()}
}

function Get-YmccLocalFanDiskTuple([string]$PowerControl) {
    $pc=Assert-YmccLocalFanPath $PowerControl 'Directory'
    $hostPayload=Read-YmccLocalFanJson (Join-Path $pc 'fan-host-v2/YeManFanHost.payload.json')
    $id=[string]$hostPayload.runtimeId
    if($id -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$'){throw 'LOCAL_FAN_DISK_RUNTIME_ID_INVALID'}
    $prefixes=@('PowerControl/fan-host-v2',('PowerControl/handheldcompanion-runtime/'+$id),'PowerControl/ymcc-shared-runtime')
    $index=@{};$documents=@{'PowerControl/fan-host-v2/YeManFanHost.payload.json'=$hostPayload}
    foreach($prefix in $prefixes){
        $root=Join-Path $pc $prefix.Substring('PowerControl/'.Length)
        foreach($row in @(Get-YmccLocalFanTree $root)){$path=$prefix+'/'+$row.path;$index[$path]=[pscustomobject]@{path=$path;bytes=$row.bytes;sha256=$row.sha256}}
    }
    foreach($path in @(('PowerControl/handheldcompanion-runtime/'+$id+'/HandheldCompanion.runtime.json'),'PowerControl/ymcc-shared-runtime/manifest.json')){$documents[$path]=Read-YmccLocalFanJson (Join-Path $pc $path.Substring('PowerControl/'.Length))}
    return Get-YmccLocalFanTuple $index $documents
}
function Assert-YmccPriorLocalFanInstall {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)]$PriorPackage,[Parameter(Mandatory=$true)][string]$InstallParent,[string]$CompletedLocalRepairReceipt='',[string]$CompletedLocalRepairSha256='',$IncomingPackage=$null)
    # Reopen the explicit pin: callers cannot accidentally reuse stale/mutated rows.
    $prior=Read-YmccPinnedLocalArchive -Archive $PriorPackage.archive -Sha256 $PriorPackage.sha256
    $parent=Assert-YmccLocalFanPath $InstallParent 'Directory'
    $identityFile=Join-Path $parent 'YeManCC/export-identity.json';$installed=Read-YmccLocalFanJson $identityFile
    if([string]$installed.buildId -cne [string]$prior.identity.buildId -or [string]$installed.version -cne [string]$prior.identity.version -or [string]$installed.sourceTreeSha256 -cne [string]$prior.identity.sourceTreeSha256){throw 'LOCAL_FAN_PRIOR_INSTALLED_BUILD_MISMATCH'}
    $installedIndex=Get-YmccLocalFanIdentityIndex $installed
    $archiveIndex=@{};foreach($row in $prior.entries){$archiveIndex[$row.path]=$row}
    # This exact receipt comparison also binds the entire installed identity ledger.
    Assert-YmccLocalFanRows @($archiveIndex['YeManCC/export-identity.json']) @((Get-YmccLocalFanFileRow $identityFile 'YeManCC/export-identity.json')) 'prior installed export identity'
    $native=Get-YmccLocalFanFileRow (Join-Path $parent 'YeManCC/YeManCC.exe') 'YeManCC/YeManCC.exe'
    Assert-YmccLocalFanRows @($archiveIndex['YeManCC/YeManCC.exe']) @($native) 'prior installed native'
    Assert-YmccLocalFanRows @($installedIndex['YeManCC/YeManCC.exe']) @($native) 'prior identity native'
    $tuple=Get-YmccLocalFanDiskTuple (Join-Path $parent 'PowerControl')
    if($CompletedLocalRepairReceipt){
        # Only an explicitly pinned terminal from this developer transaction can
        # explain the retained incoming tuple. Old native/identity checks above
        # remain exact; public updater version/recovery policy is unchanged.
        if($CompletedLocalRepairSha256 -cnotmatch '^[a-fA-F0-9]{64}$' -or !$IncomingPackage){throw 'LOCAL_FAN_RESUME_EXPLICIT_PINS_REQUIRED'}
        $repairPath=Assert-YmccLocalFanPath $CompletedLocalRepairReceipt 'File'
        $repairHash=(Get-FileHash -LiteralPath $repairPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if($repairHash -ine $CompletedLocalRepairSha256){throw 'LOCAL_FAN_RESUME_RECEIPT_PIN_CHANGED'}
        $repair=Read-YmccLocalFanJson $repairPath
        if($repair.owner -cne 'YMCC.DeveloperLocalDeployment' -or $repair.admission -cne 'EXPLICIT_LOCAL_VERIFIED_MAINLINE_PACKAGE' -or !$repair.apply -or
           $repair.status -cne 'REPAIR_REQUIRED' -or $repair.canonicalDeployExitCode -ne 0 -or
           $repair.error -cne 'LOCAL_DEPLOY_COMPLETE_PACKAGE_READBACK_FAILED: PowerControl/Sleep/player-blacklist.txt' -or
           [string]$repair.installRoot -ine (Join-Path $parent 'YeManCC') -or
           $repair.priorInstalled.sha256 -ine $prior.sha256 -or $repair.priorInstalled.identity.buildId -cne $prior.identity.buildId -or
           $repair.buildId -cne $repair.incomingTuple.identity.buildId -or $repair.sourceTreeSha256 -cne $repair.incomingTuple.identity.sourceTreeSha256 -or
           @($repair.rollback.failures).Count -ne 1 -or $repair.rollback.failures[0] -cne 'PowerControl/Sleep/player-blacklist.txt: later or unknown content preserved'){
            throw 'LOCAL_FAN_RESUME_RECEIPT_NOT_EXACT_COMPLETED_LOCAL_REPAIR'
        }
        Assert-YmccLocalFanRows $IncomingPackage.tuple.entries $repair.incomingTuple.tuple.entries 'resume receipt incoming tuple'
        Assert-YmccLocalFanRows $IncomingPackage.tuple.entries $repair.immutableRepair.new.entries 'resume completed immutable repair'
        Assert-YmccLocalFanRows $IncomingPackage.tuple.entries $tuple.entries 'resume actual retained incoming tuple'
        if((Get-FileHash -LiteralPath $repairPath -Algorithm SHA256).Hash -ine $repairHash){throw 'LOCAL_FAN_RESUME_RECEIPT_CHANGED_DURING_CHECK'}
    }else{
        if($CompletedLocalRepairSha256){throw 'LOCAL_FAN_RESUME_PATH_REQUIRED'}
        Assert-YmccLocalFanRows $prior.tuple.entries $tuple.entries 'prior installed immutable Fan/runtime/shared'
    }
    return [pscustomobject]@{mode='explicit-local-development';installParent=$parent;archive=$prior.archive;sha256=$prior.sha256;identity=$installed;native=$native;tuple=$tuple;immutablePrefixes=$tuple.immutablePrefixes}
}
function Assert-YmccIncomingLocalFanStage {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$StageRoot)
    $stage=Assert-YmccLocalFanPath $StageRoot 'Directory'
    $identity=Read-YmccLocalFanJson (Join-Path $stage 'YeManCC/export-identity.json')
    $expected=Get-YmccLocalFanIdentityIndex $identity
    $layout=Read-YmccLocalFanJson (Join-Path $stage 'YeManCC/update-manifest.json')
    if([string]$layout.packageVersion -cne [string]$identity.version){throw 'LOCAL_FAN_STAGE_VERSION_BINDING_INVALID'}
    $tuple=Get-YmccLocalFanDiskTuple (Join-Path $stage 'PowerControl')
    $documents=@{'YeManCC/update-manifest.json'=$layout}
    $hostName='PowerControl/fan-host-v2/YeManFanHost.payload.json';$runtimeName='PowerControl/handheldcompanion-runtime/'+$tuple.runtimeId+'/HandheldCompanion.runtime.json';$sharedName='PowerControl/ymcc-shared-runtime/manifest.json'
    foreach($path in @($hostName,$runtimeName,$sharedName)){$documents[$path]=Read-YmccLocalFanJson (Join-Path $stage $path)}
    $actual=@{};foreach($row in $tuple.entries){$actual[$row.path]=$row}
    foreach($path in @('YeManCC/YeManCC.exe','YeManCC/update-manifest.json')){$actual[$path]=Get-YmccLocalFanFileRow (Join-Path $stage $path) $path}
    $prefixes=$tuple.immutablePrefixes
    $managed=@($expected.Values|Where-Object{ $path=$_.path; $actual.ContainsKey($path) -or @($prefixes|Where-Object{$path.StartsWith($_+'/',[StringComparison]::OrdinalIgnoreCase)}).Count -gt 0 })
    Assert-YmccLocalFanRows $managed @($actual.Values) 'incoming stage identity closure'
    $tuple=Get-YmccLocalFanTuple $actual $documents
    # Also execute the real, read-only shared-plan manifest checks.
    function Normalize-ManifestRelativePath([string]$Path){return (ConvertTo-YmccLocalFanPath $Path).Replace('/','\')}
    . (Join-Path $PSScriptRoot '../native/updater_hc_runtime.ps1')
    $null=Get-YmccSharedRuntimePlan (Join-Path $stage 'PowerControl/ymcc-shared-runtime') $tuple.runtimeId
    return [pscustomobject]@{stageRoot=$stage;identity=$identity;native=$actual['YeManCC/YeManCC.exe'];tuple=$tuple;immutablePrefixes=$tuple.immutablePrefixes}
}

function Invoke-YmccLocalFanTransition {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$StageRoot,[Parameter(Mandatory=$true)][string]$PowerControl)
    $incoming=Assert-YmccIncomingLocalFanStage -StageRoot $StageRoot
    $pc=Assert-YmccLocalFanPath $PowerControl 'Directory'
    if((Split-Path -Leaf $pc) -ine 'PowerControl'){throw 'LOCAL_FAN_TARGET_MUST_BE_POWERCONTROL'}
    $stage=$incoming.stageRoot
    if($pc -ieq $stage -or $pc.StartsWith($stage+'\',[StringComparison]::OrdinalIgnoreCase) -or $stage.StartsWith($pc+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'LOCAL_FAN_STAGE_TARGET_OVERLAP'}
    # Require the ordinary existing host. The caller has already pinned/admitted
    # the prior tuple. A retry within that same local journal may find a partial
    # copy; retain its actual bytes as INCOMPLETE, never label it as admitted old.
    $null=Assert-YmccLocalFanPath (Join-Path $pc 'fan-host-v2') 'Directory'
    $beforeRows=[Collections.Generic.List[object]]::new()
    foreach($prefix in @('fan-host-v2','handheldcompanion-runtime','ymcc-shared-runtime')){
        $root=Join-Path $pc $prefix
        if(Test-Path -LiteralPath $root){
            foreach($row in @(Get-YmccLocalFanTree $root)){$beforeRows.Add([pscustomobject]@{path=('PowerControl/'+$prefix+'/'+$row.path);bytes=$row.bytes;sha256=$row.sha256})}
        }
    }
    try{
        $old=Get-YmccLocalFanDiskTuple $pc
        $old|Add-Member -NotePropertyName validation -NotePropertyValue 'MANIFEST_CLOSED_CURRENT_TARGET'
    }catch{
        $old=[pscustomobject]@{validation='INCOMPLETE_CURRENT_TARGET';validationError=$_.Exception.Message;entries=$beforeRows.ToArray()}
    }
    function Normalize-ManifestRelativePath([string]$Path){return (ConvertTo-YmccLocalFanPath $Path).Replace('/','\')}
    function Assert-TreeMatch([string]$Source,[string]$Target,[string]$Label){
        Assert-YmccLocalFanRows @(Get-YmccLocalFanTree $Source) @(Get-YmccLocalFanTree $Target) $Label
    }
    function Copy-TreeChecked([string]$Source,[string]$Target,[string[]]$Options){
        if(@($Options).Count -ne 4 -or (Compare-Object @('/PURGE','/IS','/IT','/IM') @($Options))){throw 'LOCAL_FAN_COPY_OPTIONS_UNSUPPORTED'}
        $sourceFull=Assert-YmccLocalFanPath $Source 'Directory';$targetFull=Assert-YmccLocalFanPath $Target
        if($sourceFull -ieq $targetFull -or $sourceFull.StartsWith($targetFull+'\',[StringComparison]::OrdinalIgnoreCase) -or $targetFull.StartsWith($sourceFull+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'LOCAL_FAN_COPY_OVERLAP'}
        $rows=@(Get-YmccLocalFanTree $sourceFull)
        if(!$rows.Count){throw 'LOCAL_FAN_COPY_SOURCE_EMPTY'}
        $oldRows=@();if(Test-Path -LiteralPath $targetFull){$oldRows=@(Get-YmccLocalFanTree $targetFull)}
        $null=[IO.Directory]::CreateDirectory($targetFull);$expected=@{};$expectedDirectories=@{}
        foreach($row in $rows){
            $relative=ConvertTo-YmccLocalFanPath $row.path;$expected[$relative]=$true
            while($relative.Contains('/')){$relative=$relative.Substring(0,$relative.LastIndexOf('/'));$expectedDirectories[$relative]=$true}
        }
        foreach($row in $oldRows){
            if($expected.ContainsKey([string]$row.path)){continue}
            $file=Assert-YmccLocalFanPath (Join-Path $targetFull $row.path) 'File'
            if(!$file.StartsWith($targetFull+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'LOCAL_FAN_COPY_PURGE_ESCAPED'}
            Remove-Item -LiteralPath $file -Force -ErrorAction Stop
        }
        # Remove only checked, now-empty surplus directories. No recursive delete.
        $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($targetFull)
        $directories=[Collections.Generic.List[string]]::new();$seen=0
        while($pending.Count){
            $directory=$pending.Pop();$null=Assert-YmccLocalFanPath $directory 'Directory'
            foreach($item in @(Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop)){
                if(++$seen -gt 50000 -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'LOCAL_FAN_COPY_PURGE_UNSAFE'}
                if($item.PSIsContainer){$directories.Add($item.FullName);$pending.Push($item.FullName)}
            }
        }
        foreach($directory in @($directories|Sort-Object Length -Descending)){
            $safe=Assert-YmccLocalFanPath $directory 'Directory'
            if(!$safe.StartsWith($targetFull+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'LOCAL_FAN_COPY_DIRECTORY_ESCAPED'}
            $relative=ConvertTo-YmccLocalFanPath $safe.Substring($targetFull.Length+1)
            if(!$expectedDirectories.ContainsKey($relative)){[IO.Directory]::Delete($safe,$false)}
        }
        foreach($row in $rows){
            $relative=ConvertTo-YmccLocalFanPath $row.path
            $from=Assert-YmccLocalFanPath (Join-Path $sourceFull $relative) 'File'
            $to=Assert-YmccLocalFanPath (Join-Path $targetFull $relative)
            if(!$to.StartsWith($targetFull+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'LOCAL_FAN_COPY_TARGET_ESCAPED'}
            $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($to))
            [IO.File]::Copy($from,$to,$true)
            Assert-YmccLocalFanRows @($row) @((Get-YmccLocalFanFileRow $to $relative)) 'immutable copy readback'
        }
        Assert-TreeMatch $sourceFull $targetFull 'immutable exact replacement'
    }
    # This source contains function definitions only. Use the actual production
    # sync, manifest checks and bounded payload-installer worker, never a fixture.
    . (Join-Path $PSScriptRoot '../native/updater_hc_runtime.ps1')
    $runtimeSource=Join-Path $stage ('PowerControl/handheldcompanion-runtime/'+$incoming.tuple.runtimeId)
    $result=Sync-YmccImmutableFanPayload -RuntimeSource $runtimeSource -HostSource (Join-Path $stage 'PowerControl/fan-host-v2') -PowerControl $pc
    $new=Get-YmccLocalFanDiskTuple $pc
    Assert-YmccLocalFanRows $incoming.tuple.entries $new.entries 'immutable transition readback'
    return [pscustomobject]@{mode='explicit-local-development';powerControl=$pc;old=$old;incoming=$incoming.tuple;new=$new;sync=$result;identity=$incoming.identity;immutablePrefixes=$new.immutablePrefixes}
}