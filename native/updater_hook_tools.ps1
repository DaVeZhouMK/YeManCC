# Explicit installation may close the two supported hooking/frame-generation
# tools. No background-download path invokes this function.
function Stop-YmccUpdateHookTools([string[]]$Paths,[ValidateRange(0,60000)][int]$GraceMilliseconds=10000,[ValidateRange(1,60000)][int]$ForceMilliseconds=10000) {
    $allowed=@{};$knownNames=@('OptiscalerClient.exe','LosslessScaling.exe')
    foreach($path in $Paths){
        if(![IO.Path]::IsPathRooted($path) -or [IO.Path]::GetFileName($path) -notin $knownNames){throw 'UPDATE_HOOK_PATH_INVALID'}
        $allowed[[IO.Path]::GetFullPath($path)]=$true
    }
    if(!$allowed.Count){return}
    if($allowed.Count -gt 66){throw 'UPDATE_HOOK_PATH_LIMIT'}
    $instances=[Collections.Generic.List[object]]::new();$self=[Diagnostics.Process]::GetCurrentProcess();try{$session=$self.SessionId}finally{$self.Dispose()}
    $deadline=[DateTime]::UtcNow.AddSeconds(60);$failure=$null
    try {
        foreach($name in @('OptiscalerClient','LosslessScaling')){
            foreach($process in @([Diagnostics.Process]::GetProcessesByName($name))){
                $retained=$false
                try {
                    $identity=Get-YmccUpdaterProcessIdentity -ProcessId $process.Id
                    if(!$identity -or $process.SessionId -ne $session){continue}
                    $actual=[IO.Path]::GetFullPath($identity.Image)
                    if(!$allowed.ContainsKey($actual)){continue}
                    if($instances.Count -ge 16){$failure='UPDATE_HOOK_PROCESS_LIMIT';continue}
                    $null=$process.Handle;$bound=Get-YmccUpdaterProcessIdentity -Process $process
                    if(!$bound){continue}
                    if($bound.Creation -cne $identity.Creation -or $bound.Image -ine $identity.Image){throw 'UPDATE_HOOK_IDENTITY_CHANGED'}
                    $instances.Add([pscustomobject]@{process=$process;path=$actual;creation=$bound.Creation});$retained=$true
                }catch{$failure=$_.Exception.Message}finally{if(!$retained){$process.Dispose()}}
            }
        }
        foreach($instance in $instances){
            $remaining=[int][Math]::Max(0,($deadline-[DateTime]::UtcNow).TotalMilliseconds)
            if($remaining -le 0){$failure='UPDATE_HOOK_EXIT_BUDGET_EXCEEDED';break}
            $grace=[Math]::Min($GraceMilliseconds,[Math]::Max(0,$remaining-1))
            $force=[Math]::Min($ForceMilliseconds,[Math]::Max(1,$remaining-$grace))
            try{$null=Stop-YmccUpdaterParent $instance.process.Id $instance.path $instance.creation $grace $force}catch{$failure=$_.Exception.Message}
        }
        # A third-party watchdog must not create an endless close/restart loop.
        foreach($name in @('OptiscalerClient','LosslessScaling')){
            foreach($process in @([Diagnostics.Process]::GetProcessesByName($name))){try{
                $identity=Get-YmccUpdaterProcessIdentity -ProcessId $process.Id
                if($identity -and $process.SessionId -eq $session -and $allowed.ContainsKey([IO.Path]::GetFullPath($identity.Image))){$failure='UPDATE_HOOK_TOOL_RESTARTED_OR_RUNNING'}
            }catch{$failure=$_.Exception.Message}finally{$process.Dispose()}}
        }
        if($failure){throw $failure}
    } finally {foreach($instance in $instances){$instance.process.Dispose()}}
}
