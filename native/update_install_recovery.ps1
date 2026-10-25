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
function Get-YmccInstallJournal {
    Assert-YmccRecoveryAcl (Split-Path -Parent $recoveryJournal)
    Assert-YmccRecoveryAcl $recoveryJournal
    if((Get-Item -LiteralPath $recoveryJournal).Length -gt 65536){throw 'UPDATE_RECOVERY_JOURNAL_TOO_LARGE'}
    $j=Get-Content -LiteralPath $recoveryJournal -Raw -Encoding UTF8 | ConvertFrom-Json
    if($j.schema -ne 1 -or $j.owner -cne 'YMCC.FullInstall' -or $j.operationId -cne $operationId -or $j.version -cne $version -or $j.sha256 -ine $sha256 -or
        [IO.Path]::GetFullPath($j.exePath).TrimEnd('\') -ine [IO.Path]::GetFullPath($exePath).TrimEnd('\') -or
        [IO.Path]::GetFullPath($j.powerControl).TrimEnd('\') -ine [IO.Path]::GetFullPath($pcDir).TrimEnd('\')){throw 'UPDATE_RECOVERY_TRANSACTION_MISMATCH'}
    return $j
}
function Set-YmccInstallJournal([string]$Phase) {
    $j=Get-YmccInstallJournal
    $j.phase=$Phase
    $self=[Diagnostics.Process]::GetCurrentProcess()
    try{
        $j|Add-Member -NotePropertyName helperPid -NotePropertyValue $self.Id -Force
        $j|Add-Member -NotePropertyName helperCreation -NotePropertyValue $self.StartTime.ToUniversalTime().ToFileTimeUtc().ToString() -Force
        $j|Add-Member -NotePropertyName helperImage -NotePropertyValue $self.MainModule.FileName -Force
        $j|Add-Member -NotePropertyName updatedAt -NotePropertyValue ([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()) -Force
    }finally{$self.Dispose()}
    $temporary=$recoveryJournal+'.tmp-'+[guid]::NewGuid().ToString('N')
    try{
        [IO.File]::WriteAllText($temporary,($j|ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
        $acl=[Security.AccessControl.FileSecurity]::new()
        $acl.SetSecurityDescriptorSddlForm('O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)')
        Set-Acl -LiteralPath $temporary -AclObject $acl -ErrorAction Stop
        Assert-YmccRecoveryAcl $temporary
        Move-Item -LiteralPath $temporary -Destination $recoveryJournal -Force -ErrorAction Stop
    }finally{if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue}}
}
