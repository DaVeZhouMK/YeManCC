param([Parameter(Mandatory=$true)][string]$Out,[string]$ComponentRoot='')
$ErrorActionPreference='Stop'
if(!$ComponentRoot){$ComponentRoot=Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl'}
$Out=[IO.Path]::GetFullPath($Out)
if(Test-Path -LiteralPath $Out){throw 'Use a fresh evidence directory'}
$null=[IO.Directory]::CreateDirectory($Out)
. (Join-Path $PSScriptRoot 'ymcc_sim/global/decky-update-recovery/appearance-library-fixture.ps1') -ComponentRoot $ComponentRoot -EvidenceRoot $Out
Initialize-ResidentAppearanceInterop
$interop='Ymcc.CssResidentProcess' -as [type]
function Assert($Condition,[string]$Message){if(!$Condition){throw $Message}}
function Match([string[]]$Paths,[string[]]$Hashes){
    try{$interop::MatchInstalledPreset($script:ThemeRoot,$Paths,$Hashes)}catch{$false}
}
function CountDrift([string[]]$Paths,[string[]]$Hashes){
    try{$interop::CountInstalledPresetDrift($script:ThemeRoot,$Paths,$Hashes)}catch{-1}
}
$script:RetiredThemeNames=@('Game Cover Reflections')
function Get-AppearanceSteamSession {}
function Get-BackendProcesses {}
function Test-BackendOwner {$false}
function Test-CssDesktopEditing {$false}
$null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($script:ThemeRoot))
Copy-Item -LiteralPath (Join-Path $script:PackRoot 'themes') -Destination $script:ThemeRoot -Recurse
Invoke-PresetPrepare|Out-Null
Complete-PresetReload
$pack=Read-PresetManifest
$paths=[string[]]$pack.files.path;$hashes=[string[]]$pack.files.sha256
$rows=@(Get-ThemeRows -RevisionOnly)
$file=Join-Path $script:ThemeRoot 'Round/shared.css'
$bytes=[IO.File]::ReadAllBytes($file)
$created=[IO.File]::GetCreationTimeUtc($file);$written=[IO.File]::GetLastWriteTimeUtc($file)
$outside=Join-Path $Out 'outside'
$link=Join-Path $script:ThemeRoot 'ResourceLink'
$null=[IO.Directory]::CreateDirectory($outside)
Copy-Item -LiteralPath $file -Destination (Join-Path $outside 'shared.css')
Assert ($interop.GetMethod('MatchInstalledPreset') -and (Match $paths $hashes)) 'The production interop must match all installed resources'
Assert ($interop.GetMethod('CountInstalledPresetDrift') -and (CountDrift $paths $hashes) -eq 0) 'The production interop must count installed drift'
$cases=[Collections.Generic.List[string]]::new()
$cases.Add('complete-installed-preset')
$originalHash=Get-ConfigHash $file
foreach($relative in '..\outside\shared.css','Round\..\Round\shared.css','Round:shared.css'){
    Assert (!(Match @($relative) @($originalHash))) ('Invalid relative path matched: '+$relative)
    Assert ((CountDrift @($relative) @($originalHash)) -eq -1) ('Invalid path reported counted drift: '+$relative)
    $cases.Add($relative)
}
Assert (!(Match @($file) @($originalHash))) 'Rooted input matched'
Assert ((CountDrift @($file) @($originalHash)) -eq -1) 'Rooted input reported counted drift'
$cases.Add('rooted-path')
try{
    $null=New-Item -ItemType Junction -Path $link -Target $outside
    Assert (!(Match @('ResourceLink/shared.css') @($originalHash))) 'A linked parent must never match, even with the expected bytes'
    Assert ((CountDrift @('Round/shared.css','ResourceLink/shared.css') @(('0'*64),$originalHash)) -eq -1) 'A mismatch cannot hide a later linked parent'
    $cases.Add('junction-parent')
}finally{if(Test-Path -LiteralPath $link){[IO.Directory]::Delete($link,$false)}}

foreach($case in 'hidden','same-metadata-bytes','missing','directory','locked','oversized'){
    $handle=$null
    try{
        switch($case){
            hidden {[IO.File]::SetAttributes($file,[IO.FileAttributes]::Hidden)}
            'same-metadata-bytes' {
                $changed=[byte[]]$bytes.Clone();$changed[0]=$changed[0] -bxor 1
                [IO.File]::WriteAllBytes($file,$changed)
                [IO.File]::SetCreationTimeUtc($file,$created);[IO.File]::SetLastWriteTimeUtc($file,$written)
                Assert (([IO.FileInfo]::new($file)).Length -eq $bytes.Length -and [IO.File]::GetCreationTimeUtc($file) -eq $created -and [IO.File]::GetLastWriteTimeUtc($file) -eq $written) 'Same-metadata fixture did not preserve metadata'
            }
            missing {[IO.File]::Delete($file)}
            directory {[IO.File]::Delete($file);$null=[IO.Directory]::CreateDirectory($file)}
            locked {$handle=[IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)}
            oversized {
                $grow=[IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::None)
                try{$grow.SetLength(16777217)}finally{$grow.Dispose()}
            }
        }
        $shouldMatch=$case -ceq 'hidden'
        Assert ((Match $paths $hashes) -eq $shouldMatch) ('Wrong batch outcome: '+$case)
        $expectedCount=if($shouldMatch){0}elseif($case -ceq 'same-metadata-bytes'){1}else{-1}
        Assert ((CountDrift $paths $hashes) -eq $expectedCount) ('Wrong drift eligibility: '+$case)
        $status=Get-PresetMetadata $rows
        Assert ($status.packPrepared -eq $shouldMatch -and $status.packRepairRequired -ne $shouldMatch -and $status.canRestore) ('Fallback did not preserve metadata status: '+$case)
        if($case -in 'locked','oversized'){Assert ([bool]$status.packError) ('Fallback lost the original diagnostic: '+$case)}
        $cases.Add($case)
    }finally{
        if($handle){$handle.Dispose()}
        if([IO.Directory]::Exists($file)){[IO.Directory]::Delete($file,$false)}
        if([IO.File]::Exists($file)){[IO.File]::SetAttributes($file,[IO.FileAttributes]::Normal)}
        [IO.File]::WriteAllBytes($file,$bytes)
    }
}
Assert ((Match $paths $hashes) -and (Get-PresetMetadata $rows).packPrepared) 'Healthy recovery after faults failed'
$cases.Add('healthy-after-faults')
& {
    $other=Join-Path $script:ThemeRoot 'Art Hero/shared.css'
    $otherBytes=[IO.File]::ReadAllBytes($other)
    $readResource=${function:Get-PresetResourceHash};$installedCalls=[int[]]::new(1)
    function Get-PresetResourceHash([string]$Path,[string]$InvalidFileMessage='',[switch]$InstalledTarget,[ref]$Raw,[switch]$SchemaDigestOnly){
        if($InstalledTarget){$installedCalls[0]++}
        & $readResource @PSBoundParameters
    }
    try{
        [IO.File]::AppendAllText($file,"`n/* fixture drift */")
        [IO.File]::AppendAllText($other,"`n/* second fixture drift */")
        $status=Get-PresetMetadata $rows
        Assert ((CountDrift $paths $hashes) -eq 2 -and $status.packRepairRequired -and !$status.packError -and !$installedCalls[0]) 'Two ordinary mismatches must not send the whole pack through scalar validation'
        $cases.Add('two-mismatches-with-zero-scalar-resource-reads')
        $lock=[IO.File]::Open($file,'Open','Read','None')
        try{
            $status=Get-PresetMetadata $rows
            Assert ((CountDrift $paths $hashes) -eq -1 -and $status.packRepairRequired -and $status.packError -and $installedCalls[0] -gt 0) 'Earlier content drift cannot hide a later unreadable file'
        }finally{$lock.Dispose()}
        $cases.Add('mismatch-then-lock-retains-ordered-diagnostic')
        $ownerPath=Join-Path (Get-PresetStateDir) 'owner.json'
        $ownerBytes=[IO.File]::ReadAllBytes($ownerPath)
        $owner=Read-PresetState
        $owner.packDigest=if($pack.digest -eq ('0'*64)){'1'*64}else{'0'*64}
        try{
            Save-PresetState $owner
            $installedCalls[0]=0
            $status=Get-PresetMetadata $rows
            Assert ($status.packUpdateAvailable -and $status.packRepairRequired -and !$status.packPrepared -and !$status.packError) 'A new bundle still requires repair after an ordinary content difference'
            Assert (!$installedCalls[0]) 'An earlier owned bundle must use batch validation for ordinary changes'
            $cases.Add('previous-owned-version-with-content-drift-uses-batch')
            $lock=[IO.File]::Open($file,'Open','Read','None')
            try{
                $status=Get-PresetMetadata $rows
                Assert ($status.packUpdateAvailable -and $status.packRepairRequired -and $status.packError -and $installedCalls[0] -gt 0) 'Version upgrade must keep the ordered unreadable-file diagnostic'
            }finally{$lock.Dispose()}
            $cases.Add('previous-owned-version-with-unreadable-file-falls-back')
            [IO.File]::WriteAllBytes($file,$bytes)
            [IO.File]::WriteAllBytes($other,$otherBytes)
            $installedCalls[0]=0
            $status=Get-PresetMetadata $rows
            Assert ($status.packUpdateAvailable -and $status.packRepairRequired -and !$status.packPrepared -and !$status.packError -and !$installedCalls[0]) 'Matching resources do not promote an old ownership receipt'
            Assert ((Read-PresetState).packDigest -eq $owner.packDigest) 'Metadata observation must not advance ownership'
            $cases.Add('previous-owned-version-with-matching-content-retains-owner-gate')
        }finally{[IO.File]::WriteAllBytes($ownerPath,$ownerBytes)}
    }finally{[IO.File]::WriteAllBytes($file,$bytes);[IO.File]::WriteAllBytes($other,$otherBytes)}
}
$backupPath=(Read-PresetState).originalBackup
$backup=Read-PresetBackup $backupPath
$first=@($backup.journal.entries|Where-Object existed)[0]
$blob=Join-Path $backupPath ('files/'+$first.blob)
$backupBytes=[IO.File]::ReadAllBytes($blob)
$backupCreated=[IO.File]::GetCreationTimeUtc($blob);$backupWritten=[IO.File]::GetLastWriteTimeUtc($blob)
& {
    $readHash=${function:Get-ConfigHash};$scalarCalls=[int[]]::new(1)
    function Get-ConfigHash([string]$Path,[long]$MaximumBytes=0,$ObservedLength=$null){
        $scalarCalls[0]++
        & $readHash @PSBoundParameters
    }
    Assert ((Read-PresetBackup $backupPath).journal.entries.Count -eq $backup.journal.entries.Count -and !$scalarCalls[0]) 'Healthy backup did not use batch validation'
    try{
        $changed=[byte[]]$backupBytes.Clone();$changed[0]=$changed[0] -bxor 1
        [IO.File]::WriteAllBytes($blob,$changed)
        [IO.File]::SetCreationTimeUtc($blob,$backupCreated);[IO.File]::SetLastWriteTimeUtc($blob,$backupWritten)
        Assert (([IO.FileInfo]::new($blob)).Length -eq $backupBytes.Length -and [IO.File]::GetCreationTimeUtc($blob) -eq $backupCreated -and [IO.File]::GetLastWriteTimeUtc($blob) -eq $backupWritten) 'Backup replacement fixture changed metadata'
        $errorText='';try{Read-PresetBackup $backupPath|Out-Null}catch{$errorText=$_.Exception.Message}
        Assert ($errorText -ceq 'Preset backup integrity check failed' -and $scalarCalls[0] -gt 0) 'Backup replacement must be detected and use the original diagnosis'
    }finally{[IO.File]::WriteAllBytes($blob,$backupBytes)}
    $scalarCalls[0]=0
    Assert ((Read-PresetBackup $backupPath).journal.entries.Count -eq $backup.journal.entries.Count -and !$scalarCalls[0]) 'Backup validation did not recover after replacement'
}
$cases.Add('backup-batch-reread-fallback-and-recovery')
$originalPack=$script:PackRoot
$bundle=Join-Path $Out 'bundle-copy'
Copy-Item -LiteralPath $originalPack -Destination $bundle -Recurse
try{
    $script:PackRoot=$bundle
    & {
        $readResource=${function:Get-PresetResourceHash};$scalarCalls=[int[]]::new(1)
        function Get-PresetResourceHash([string]$Path,[string]$InvalidFileMessage='',[switch]$InstalledTarget,[ref]$Raw,[switch]$SchemaDigestOnly){
            if(!$InstalledTarget){$scalarCalls[0]++}
            & $readResource @PSBoundParameters
        }
        $memo=@{}
        $cold=Read-PresetManifest -AutomaticPlanMemo $memo
        $warm=Read-PresetManifest -AutomaticPlanMemo $memo
        Assert ($cold.digest -ceq $pack.digest -and $warm.digest -ceq $pack.digest -and !$scalarCalls[0]) 'Bundled resources did not use batch validation'
        $asset=Join-Path $bundle 'themes/Round/shared.css'
        $assetBytes=[IO.File]::ReadAllBytes($asset)
        $created=[IO.File]::GetCreationTimeUtc($asset);$written=[IO.File]::GetLastWriteTimeUtc($asset)
        try{
            $changed=[byte[]]$assetBytes.Clone();$changed[0]=$changed[0] -bxor 1
            [IO.File]::WriteAllBytes($asset,$changed)
            [IO.File]::SetCreationTimeUtc($asset,$created);[IO.File]::SetLastWriteTimeUtc($asset,$written)
            Assert (([IO.FileInfo]::new($asset)).Length -eq $assetBytes.Length -and [IO.File]::GetCreationTimeUtc($asset) -eq $created -and [IO.File]::GetLastWriteTimeUtc($asset) -eq $written) 'Bundle replacement fixture changed metadata'
            $errorText='';try{Read-PresetManifest -AutomaticPlanMemo $memo|Out-Null}catch{$errorText=$_.Exception.Message}
            Assert ($errorText -ceq 'Bundled preset checksum failed: Round\shared.css' -and $scalarCalls[0] -gt 0) 'Same-metadata bundle replacement must be rejected with the original diagnosis'
        }finally{[IO.File]::WriteAllBytes($asset,$assetBytes)}
        $scalarCalls[0]=0
        Assert ((Read-PresetManifest -AutomaticPlanMemo $memo).digest -ceq $pack.digest -and !$scalarCalls[0]) 'Bundle validation did not recover'
    }
}finally{$script:PackRoot=$originalPack}
$cases.Add('bundled-batch-reread-fallback-and-recovery')
[pscustomobject]@{ok=$true;engine=[string]$PSVersionTable.PSVersion;cases=$cases.ToArray();resources=$paths.Count;residentHash=(Get-FileHash -LiteralPath (Join-Path $ComponentRoot 'steam-appearance/resident.ps1')).Hash;realSteamOperations=0;realCssOperations=0}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
Write-Output ('PASS: '+$cases.Count+' installed resource, backup, and bundle batch checks')
