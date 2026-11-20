# Embedded from current native source. Cached LocalAppData helpers never serve
# as authorization for a same-version installation.
function Assert-YmccRecoveryAcl([string]$Path) {
    $item=Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'UPDATE_RECOVERY_REPARSE_POINT'}
    $acl=Get-Acl -LiteralPath $Path -ErrorAction Stop
    $allowed=@('S-1-5-18','S-1-5-32-544')
    if(!$acl.AreAccessRulesProtected -or $acl.GetOwner([Security.Principal.SecurityIdentifier]).Value -notin $allowed){throw 'UPDATE_RECOVERY_UNPROTECTED_OWNER'}
    $seen=@{}
    foreach($rule in $acl.GetAccessRules($true,$true,[Security.Principal.SecurityIdentifier])) {
        if($rule.AccessControlType -ne 'Allow' -or $rule.IdentityReference.Value -notin $allowed){throw 'UPDATE_RECOVERY_UNPROTECTED_ACCESS'}
        $seen[$rule.IdentityReference.Value]=$true
    }
    if(!$seen['S-1-5-18'] -or !$seen['S-1-5-32-544']){throw 'UPDATE_RECOVERY_MISSING_ACCESS'}
}
function Test-YmccRecoveryInteger($Value,[uint64]$Maximum=[int64]::MaxValue){
    return ($Value -is [byte] -or $Value -is [sbyte] -or $Value -is [int16] -or $Value -is [uint16] -or $Value -is [int32] -or $Value -is [uint32] -or $Value -is [int64] -or $Value -is [uint64]) -and $Value -ge 0 -and $Value -le $Maximum
}
function Assert-YmccRecoveryDepth($Value,[int]$Depth=0){
    if($Depth -gt 32){throw 'UPDATE_RECOVERY_DEPTH_LIMIT'}
    if($Value -is [pscustomobject]){foreach($property in $Value.PSObject.Properties){Assert-YmccRecoveryDepth $property.Value ($Depth+1)}}
    elseif($Value -is [Collections.IDictionary]){foreach($key in $Value.Keys){Assert-YmccRecoveryDepth $Value[$key] ($Depth+1)}}
    elseif($Value -is [array]){foreach($item in $Value){Assert-YmccRecoveryDepth $item ($Depth+1)}}
}
function Assert-YmccRecoveryDocument($Journal){
    Assert-YmccRecoveryDepth $Journal
    if(!$Journal -or $Journal -isnot [pscustomobject] -or !(Test-YmccRecoveryInteger $Journal.schema 1) -or $Journal.schema -ne 1 -or $Journal.owner -isnot [string] -or $Journal.owner -cne 'YMCC.FullInstall'){throw 'UPDATE_RECOVERY_SCHEMA_INVALID'}
    if($Journal.PSObject.Properties['schemaVersion'] -and (!(Test-YmccRecoveryInteger $Journal.schemaVersion 1) -or $Journal.schemaVersion -ne 1)){throw 'UPDATE_RECOVERY_SCHEMA_INVALID'}
    foreach($key in @('operationId','version','sha256','exePath','powerControl','phase')){
        $value=$Journal.$key;$limit=if($key -eq 'operationId'){128}else{32768}
        if($value -isnot [string] -or !$value.Length -or $value.Length -gt $limit -or $value.IndexOf([char]0) -ge 0){throw ('UPDATE_RECOVERY_FIELD_INVALID_'+$key)}
    }
    if($Journal.version -cnotmatch '^(0|[1-9][0-9]{0,9})\.(0|[1-9][0-9]{0,9})\.(0|[1-9][0-9]{0,9})$' -or @($Journal.version.Split('.')|Where-Object{[uint64]$_ -gt [int32]::MaxValue}).Count -or $Journal.sha256 -cnotmatch '^[a-fA-F0-9]{64}$'){throw 'UPDATE_RECOVERY_ID_INVALID'}
    if($Journal.phase -cnotin @('authorized','preparing','applying','repair-required','committed','rolled-back','failed')){throw 'UPDATE_RECOVERY_PHASE_INVALID'}
    foreach($key in @('exePath','powerControl','launcherImage','helperImage')){
        if(!$Journal.PSObject.Properties[$key]){continue};$value=$Journal.$key
        if($value -isnot [string] -or !$value.Length -or $value.Length -gt 32768 -or $value.IndexOf([char]0) -ge 0 -or ![IO.Path]::IsPathRooted($value)){throw 'UPDATE_RECOVERY_PATH_INVALID'}
        $null=[IO.Path]::GetFullPath($value)
    }
    foreach($key in @('authorizedAt','updatedAt')){if($Journal.PSObject.Properties[$key] -and !(Test-YmccRecoveryInteger $Journal.$key)){throw 'UPDATE_RECOVERY_TIME_INVALID'}}
    foreach($key in @('launcherPid','helperPid')){if($Journal.PSObject.Properties[$key] -and !(Test-YmccRecoveryInteger $Journal.$key ([uint32]::MaxValue))){throw 'UPDATE_RECOVERY_PID_INVALID'}}
    foreach($key in @('launcherCreation','helperCreation')){if($Journal.PSObject.Properties[$key] -and ($Journal.$key -isnot [string] -or $Journal.$key -cnotmatch '^[0-9]{1,20}$')){throw 'UPDATE_RECOVERY_CREATION_INVALID'}}
}
function Test-YmccRecoverySharingViolation($ErrorRecord,[switch]$Replacing){
    $exception=$ErrorRecord.Exception
    while($exception.InnerException){$exception=$exception.InnerException}
    $code=$exception.HResult -band 65535
    return ($code -in @(32,33) -or ($Replacing -and $code -in @(5,1175)))
}
function Get-YmccRecoverySnapshot([string]$Path){
    for($attempt=0;$attempt -lt 6;$attempt++){
        try{return (Get-YmccRecoverySnapshotOnce $Path)}catch{
            if(!(Test-YmccRecoverySharingViolation $_) -or $attempt -eq 5){throw}
            Start-Sleep -Milliseconds 50
        }
    }
}
function Get-YmccRecoverySnapshotOnce([string]$Path){
    Assert-YmccRecoveryAcl (Split-Path -Parent $Path);Assert-YmccRecoveryAcl $Path
    if(([IO.File]::GetAttributes($Path) -band ([IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint)) -ne 0){throw 'UPDATE_RECOVERY_NOT_REGULAR'}
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try{
        if($stream.Length -lt 1 -or $stream.Length -gt 65536){throw 'UPDATE_RECOVERY_JOURNAL_SIZE_INVALID'}
        $bytes=[byte[]]::new([int]$stream.Length);$offset=0
        while($offset -lt $bytes.Length){$count=$stream.Read($bytes,$offset,$bytes.Length-$offset);if(!$count){throw 'UPDATE_RECOVERY_JOURNAL_SHORT_READ'};$offset+=$count}
        if($stream.ReadByte() -ne -1 -or $stream.Length -ne $bytes.Length){throw 'UPDATE_RECOVERY_JOURNAL_CHANGED'}
    }finally{$stream.Dispose()}
    $skip=0;$encoding=[Text.UTF8Encoding]::new($false,$true)
    if($bytes.Length -ge 2 -and $bytes[0] -eq 255 -and $bytes[1] -eq 254){$skip=2;$encoding=[Text.UnicodeEncoding]::new($false,$true,$true)}
    elseif($bytes.Length -ge 2 -and $bytes[0] -eq 254 -and $bytes[1] -eq 255){$skip=2;$encoding=[Text.UnicodeEncoding]::new($true,$true,$true)}
    elseif($bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191){$skip=3}
    $text=$encoding.GetString($bytes,$skip,$bytes.Length-$skip)
    Initialize-YmccModuleJournalJson;[Ymcc.ModuleJournalJson]::AssertObject($text)
    $j=$text|ConvertFrom-Json -ErrorAction Stop;Assert-YmccRecoveryDocument $j
    return [pscustomobject]@{Document=$j;Bytes=$bytes;Text=$text}
}
function Get-YmccInstallJournal {
    $j=(Get-YmccRecoverySnapshot $recoveryJournal).Document
    if($j.operationId -cne $operationId -or $j.version -cne $version -or $j.sha256 -ine $sha256 -or
        [IO.Path]::GetFullPath($j.exePath).TrimEnd('\') -ine [IO.Path]::GetFullPath($exePath).TrimEnd('\') -or
        [IO.Path]::GetFullPath($j.powerControl).TrimEnd('\') -ine [IO.Path]::GetFullPath($pcDir).TrimEnd('\')){throw 'UPDATE_RECOVERY_TRANSACTION_MISMATCH'}
    return $j
}
function Test-YmccRecoveryBytes([byte[]]$Left,[byte[]]$Right){
    if($Left.Length -ne $Right.Length){return $false};for($i=0;$i -lt $Left.Length;$i++){if($Left[$i] -ne $Right[$i]){return $false}};return $true
}
function Write-YmccRecoveryFile([string]$Path,[byte[]]$Bytes) {
    # CreateNew never replaces a pre-existing backup. The protected parent is
    # still required; apply the explicit file ACL before writing journal bytes.
    $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{
        $acl=[Security.AccessControl.FileSecurity]::new();$acl.SetSecurityDescriptorSddlForm('O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)')
        Set-Acl -LiteralPath $Path -AclObject $acl -ErrorAction Stop;Assert-YmccRecoveryAcl $Path
        $stream.Write($Bytes,0,$Bytes.Length);$stream.Flush($true)
    }finally{$stream.Dispose()}
}
# Diagnostics only. The protected directory scopes this bounded snapshot to
# the installed executable; it is never read for recovery or authorization.
function Add-YmccInstallIssue([string]$Step, $Failure, [switch]$Core) {
    if (!$script:ymccInstallIssues) { $script:ymccInstallIssues=[ordered]@{} }
    $script:ymccInstallIssues[$Step]=1+[int]$script:ymccInstallIssues[$Step]
    if ($Core) { $script:ymccCoreWriteFailures++ }
    Write-Warning ('UPDATE_CONTINUED ['+$Step+']: '+$Failure.Exception.Message) -WarningAction Continue
}
function Write-YmccInstallState([string]$Phase, [int]$ProcessId=0) {
    try {
        $snapshot=[ordered]@{phase=$Phase;version=$version}
        if ($ProcessId) { $snapshot.pid=$ProcessId }
        if ($script:ymccInstallIssues) { $snapshot.issues=$script:ymccInstallIssues }
        Write-Utf8Atomic $state ($snapshot | ConvertTo-Json -Compress) 2
    } catch { Add-YmccInstallIssue 'write-state' $_ }
}
# Only package-declared destinations reach this function. Keep the old name live
# until an atomic replace succeeds, except for a directory at an exact file path.
function Clear-YmccManagedReadOnly([string]$Path) {
    $flags=[IO.File]::GetAttributes($Path)
    if ($flags -band [IO.FileAttributes]::ReadOnly) { [IO.File]::SetAttributes($Path,($flags -band (-bnot [IO.FileAttributes]::ReadOnly))) }
}
function Copy-YmccManagedFile([string]$Source, [string]$Root, [string]$Relative, [string]$Bucket, [switch]$Core) {
    $temporary=$null
    try {
        $relativePath=$Relative.Replace('\','/')
        $target=Resolve-YmccDeckyPath $Root $relativePath
        $parent=[IO.Path]::GetDirectoryName($target)
        $null=[IO.Directory]::CreateDirectory($parent)
        $expected=Get-YmccDeckyHash $Source
        $temporary=Join-Path $parent ('.ymcc-incoming-'+[guid]::NewGuid().ToString('N'))
        [IO.File]::Copy($Source,$temporary,$false)
        Clear-YmccManagedReadOnly $temporary
        if ((Get-YmccDeckyHash $temporary) -cne $expected) { throw 'UPDATE_STAGED_FILE_HASH_MISMATCH' }
        $target=Resolve-YmccDeckyPath $Root $relativePath
        $entry=[pscustomobject]@{root=$Root;relative=$relativePath;expected=$expected;backup=$null;retired=$null;lost=$false;written=$false;readOnly=$false}
        $ordinaryFileLedger.Add($entry)
        if ([IO.Directory]::Exists($target)) {
            $entry.retired=Join-Path $parent ('.ymcc-retired-'+[guid]::NewGuid().ToString('N'))
            [IO.Directory]::Move($target,$entry.retired)
        } elseif ([IO.File]::Exists($target)) {
            # A corrupt/unreadable preimage is not an install requirement. Do
            # not publish a partial backup or claim rollback if it was lost.
            try {
                $preimage=Resolve-YmccDeckyPath (Join-Path $rollbackFiles $Bucket) $relativePath
                $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($preimage))
                [IO.File]::Copy($target,$preimage,$false)
                $entry.readOnly=[bool]([IO.File]::GetAttributes($preimage) -band [IO.FileAttributes]::ReadOnly)
                Clear-YmccManagedReadOnly $preimage
                $entry.backup=$preimage
            } catch { $entry.lost=$true; Add-YmccInstallIssue 'old-file-backup' $_ }
        }
        for ($attempt=0; $attempt -lt 3; $attempt++) {
            try {
                $target=Resolve-YmccDeckyPath $Root $relativePath
                if ([IO.File]::Exists($target)) {
                    Clear-YmccManagedReadOnly $target
                    [IO.File]::Replace($temporary,$target,[NullString]::Value)
                } else { [IO.File]::Move($temporary,$target) }
                $entry.written=$true
                break
            } catch { if ($attempt -eq 2) { throw }; Start-Sleep -Milliseconds 100 }
        }
        if ((Get-YmccDeckyHash $target) -cne $expected) { throw 'UPDATE_INSTALLED_FILE_HASH_MISMATCH' }
    } catch { Add-YmccInstallIssue $Bucket $_ -Core:$Core }
    finally { if ($temporary -and [IO.File]::Exists($temporary)) { try { [IO.File]::Delete($temporary) } catch { Add-YmccInstallIssue 'temp-cleanup' $_ } } }
}
function Copy-YmccManagedTree([string]$Source, [string]$Root, [string]$Bucket, [string[]]$Excluded, [switch]$Core) {
    foreach ($file in Get-ChildItem -LiteralPath $Source -Recurse -File -Force) {
        $relative=$file.FullName.Substring($Source.Length).TrimStart('\')
        if ($relative -in $Excluded -or @($Excluded | Where-Object { $relative.StartsWith($_+'\',[StringComparison]::OrdinalIgnoreCase) }).Count) { continue }
        # The executable is installed separately. Optional persisted settings
        # and feature sidecars in the program tree cannot veto its startup.
        $required=$Core -and ($relative -in @('index.html','version.json') -or $relative -match '^assets\\.*\.(?:js|css)$' -or $file.Extension -ieq '.dll')
        Copy-YmccManagedFile $file.FullName $Root $relative $Bucket -Core:$required
    }
}
function Restore-OrdinaryFiles {
    $failures=0
    for ($i=$ordinaryFileLedger.Count-1; $i -ge 0; $i--) {
        $entry=$ordinaryFileLedger[$i]
        try {
            if (!$entry.written -and (!$entry.retired -or ![IO.Directory]::Exists($entry.retired))) { continue }
            $target=Resolve-YmccDeckyPath $entry.root $entry.relative
            if ($entry.written -and (![IO.File]::Exists($target) -or (Get-YmccDeckyHash $target) -cne $entry.expected)) { throw 'UPDATE_ROLLBACK_TARGET_CHANGED' }
            if ($entry.written -and $entry.lost) { throw 'UPDATE_ROLLBACK_PREIMAGE_UNAVAILABLE' }
            if ($entry.backup) {
                Set-YmccDeckyAtomic $entry.root $entry.relative $entry.backup $null (Get-YmccDeckyHash $entry.backup)
                if ($entry.readOnly) { [IO.File]::SetAttributes($target,([IO.File]::GetAttributes($target) -bor [IO.FileAttributes]::ReadOnly)) }
            }
            elseif ($entry.written) { [IO.File]::Delete($target) }
            if ($entry.retired) { [IO.Directory]::Move($entry.retired,$target) }
        } catch { $failures++; Add-YmccInstallIssue 'rollback-file' $_ }
    }
    if ($failures) { throw ('UPDATE_ROLLBACK_FILES_INCOMPLETE: '+$failures) }
}
function Write-YmccInstallTrace([string]$Outcome='running', $Failure=$null,
    [string]$Rollback='not-attempted', [string]$Relaunch='not-attempted') {
    try {
        if (!$script:ymccInstallStep) { return }
        if ($null -eq $script:ymccTraceAuthorizedAt) {
            $j=(Get-YmccRecoverySnapshotOnce $recoveryJournal).Document
            if ($j.operationId -cne $operationId -or $j.version -cne $version -or $j.sha256 -ine $sha256 -or
                !(Test-YmccRecoveryInteger $j.authorizedAt)) { return }
            $script:ymccTraceAuthorizedAt=$j.authorizedAt
        }
        $row=[ordered]@{schemaVersion=1; operationId=$operationId; version=$version; sha256=$sha256;
            authorizedAt=$script:ymccTraceAuthorizedAt;
            step=$script:ymccInstallStep; outcome=$Outcome; rollback=$Rollback; relaunch=$Relaunch;
            updatedAt=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()}
        if ($parentExitMode -cin @('already-exited','graceful','forced-owned-parent')) { $row.parentExit=$parentExitMode }
        if ($script:ymccInstallIssues) { $row.issues=$script:ymccInstallIssues }
        if ($Outcome -eq 'failed') {
            $row.errorKind=switch ($script:ymccInstallStep) {
                'launch-new' { 'startup-launch' }
                'await-handshake' { if ($script:ymccStartupFailure) { $script:ymccStartupFailure } else { 'handshake-check' } }
                default { 'step-failed' }
            }
            if ($script:ymccStartupFailure -ceq 'startup-exited' -and $null -ne $script:ymccStartupExitCode) {
                $row.exitCode=$script:ymccStartupExitCode
            }
            if ($Failure -and $Failure.Exception) {
                $exception=$Failure.Exception
                for ($depth=0; $depth -lt 8 -and $exception.InnerException; $depth++) { $exception=$exception.InnerException }
                $row.hresult=[int]$exception.HResult
                if ($exception -is [ComponentModel.Win32Exception]) { $row.win32Code=[int]$exception.NativeErrorCode }
            }
            $row.immutableRepairPending=[bool]$immutableRepairPending
        }
        $bytes=[Text.UTF8Encoding]::new($false,$true).GetBytes(($row | ConvertTo-Json -Compress))
        if ($bytes.Length -gt 4096) { return }
        $parent=Split-Path -Parent $recoveryJournal
        Assert-YmccRecoveryAcl $parent
        $path=Resolve-YmccDeckyPath $parent ((Split-Path -Leaf $recoveryJournal)+'.install-trace.json')
        $temporary=$path+'.tmp-'+[guid]::NewGuid().ToString('N')
        try {
            Write-YmccRecoveryFile $temporary $bytes
            if ([IO.File]::Exists($path)) {
                Assert-YmccRecoveryAcl $path
                [IO.File]::Replace($temporary,$path,[NullString]::Value)
            } else { [IO.File]::Move($temporary,$path) }
        } finally {
            if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
        }
    } catch { } # No retries, warnings, or failures enter the installation flow.
}
function Set-YmccInstallCheckpoint([string]$Step) {
    $script:ymccInstallStep=$Step
    Write-YmccInstallTrace
}
function Set-YmccInstallJournal([string]$Phase) {
    $before=Get-YmccRecoverySnapshot $recoveryJournal;$j=Get-YmccInstallJournal
    $current=Get-YmccRecoverySnapshot $recoveryJournal
    if($before.Text -cne $current.Text){throw 'UPDATE_RECOVERY_SOURCE_CHANGED'}
    $allowed=@{authorized=@('preparing','repair-required');preparing=@('preparing','applying','repair-required');applying=@('applying','committed','repair-required');'repair-required'=@('repair-required','preparing')}
    if(!$allowed.ContainsKey($j.phase) -or $Phase -cnotin $allowed[$j.phase]){throw 'UPDATE_RECOVERY_PHASE_TRANSITION_INVALID'}
    $j.phase=$Phase;$self=[Diagnostics.Process]::GetCurrentProcess()
    try{
        $j|Add-Member -NotePropertyName helperPid -NotePropertyValue $self.Id -Force
        $j|Add-Member -NotePropertyName helperCreation -NotePropertyValue $self.StartTime.ToUniversalTime().ToFileTimeUtc().ToString() -Force
        $j|Add-Member -NotePropertyName helperImage -NotePropertyValue $self.MainModule.FileName -Force
        $j|Add-Member -NotePropertyName updatedAt -NotePropertyValue ([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()) -Force
    }finally{$self.Dispose()}
    Assert-YmccRecoveryDocument $j;$text=$j|ConvertTo-Json -Depth 32 -Compress
    Initialize-YmccModuleJournalJson;[Ymcc.ModuleJournalJson]::AssertObject($text)
    $bytes=[Text.UTF8Encoding]::new($false,$true).GetBytes($text);if($bytes.Length -gt 65536){throw 'UPDATE_RECOVERY_JOURNAL_SIZE_INVALID'}
    $temporary=$recoveryJournal+'.tmp-'+[guid]::NewGuid().ToString('N')
    if(!(Test-YmccRecoveryBytes $before.Bytes (Get-YmccRecoverySnapshot $recoveryJournal).Bytes)){throw 'UPDATE_RECOVERY_SOURCE_CHANGED'}
    try{
        Write-YmccRecoveryFile $temporary $bytes
        if(!(Test-YmccRecoveryBytes $bytes (Get-YmccRecoverySnapshot $temporary).Bytes)){throw 'UPDATE_RECOVERY_TEMP_UNCONFIRMED'}
        for($attempt=0;$attempt -lt 6;$attempt++){
            try{
                if(!(Test-YmccRecoveryBytes $before.Bytes (Get-YmccRecoverySnapshot $recoveryJournal).Bytes)){throw 'UPDATE_RECOVERY_SOURCE_CHANGED'}
                # Replace publishes on the same volume without deleting the old
                # authority first. Never fall back to remove-then-move.
                [IO.File]::Replace($temporary,$recoveryJournal,[NullString]::Value)
                break
            }catch{
                if(!(Test-YmccRecoverySharingViolation $_ -Replacing) -or $attempt -eq 5){throw}
                Start-Sleep -Milliseconds 50
            }
        }
        if(!(Test-YmccRecoveryBytes $bytes (Get-YmccRecoverySnapshot $recoveryJournal).Bytes)){throw 'UPDATE_RECOVERY_READBACK_FAILED'}
    }finally{
        if([IO.File]::Exists($temporary)){try{[IO.File]::Delete($temporary)}catch{Write-Warning 'UPDATE_RECOVERY_TEMP_CLEANUP_PENDING'}}
    }
}
