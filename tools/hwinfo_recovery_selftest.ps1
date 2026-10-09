[CmdletBinding()]
param([string]$RecoveryScript)

$ErrorActionPreference = 'Stop'
if (-not $RecoveryScript) { $RecoveryScript = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '..\PowerControl\YeManHWiNFO.ps1' }
# Match the production Windows PowerShell 5.1 host even when launched from pwsh.
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + ';' + $env:PSModulePath
$RecoveryScript = [IO.Path]::GetFullPath($RecoveryScript)
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($RecoveryScript, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
# Execute actual production functions, not a parallel model of the transaction.
# Exclude the script's exit statement and mock only OS process/UAC boundaries.
$script:tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('YMCC HWiNFO Recovery Test ' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
$definitions = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] }, $false) | ForEach-Object { $_.Extent.Text })
# A real file preserves PSCommandPath/PSScriptRoot inside the extracted functions,
# and its space-containing path exercises the UAC argument quoting at runtime.
$libraryPath = Join-Path $tempRoot 'Production Recovery Functions.ps1'
[IO.File]::WriteAllText($libraryPath, ($definitions -join "`r`n"))
. $libraryPath
$script:ProductionHash = ${function:Get-HWiNFOConfigHash}
$script:SharedMemorySource = ($ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Test-HWiNFOSharedMemory' }, $false)).Body.Extent.Text
$script:installRoot = Join-Path $tempRoot 'Program Files\HWiNFO64'
New-Item -ItemType Directory -Path (Join-Path $installRoot 'YeMan') -Force | Out-Null
$script:exePath = Join-Path $installRoot 'HWiNFO64.exe'
$script:srcPath = Join-Path $installRoot 'YeMan\HWiNFO64.INI'
$script:dstPath = Join-Path $installRoot 'HWiNFO64.INI'
[IO.File]::WriteAllText($exePath, 'mock executable - never run')

function Assert([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}
function Assert-Order([string[]]$expected) {
    $cursor = -1
    foreach ($event in $expected) {
        $next = -1
        for ($i = $cursor + 1; $i -lt $script:state.Events.Count; $i++) {
            if ($script:state.Events[$i] -eq $event) { $next = $i; break }
        }
        Assert ($next -gt $cursor) ("Missing/out-of-order '{0}': {1}" -f $event, ($script:state.Events -join ', '))
        $cursor = $next
    }
}
function New-TestProcess([int]$id) {
    $process = [pscustomobject]@{ Id = $id; Path = $script:exePath; ProcessorAffinity = [IntPtr]::Zero }
    $process | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value {
        param($milliseconds)
        [void]$script:state.Events.Add('exit-confirmed')
        if (-not $script:state.WaitResult) { return $false }
        [void]$script:state.Running.Remove($this)
        return $true
    }
    return $process
}
function Reset-Test([bool]$running = $true, [bool]$healthy = $false) {
    $script:Mode = 'restart'
    $script:Elevated = $false
    $script:state = @{
        Events = (New-Object Collections.ArrayList)
        Running = (New-Object Collections.ArrayList)
        Healthy = $healthy; HealthyAfterStart = $true; HealthDelay = 0; HealthPolls = 0
        WaitResult = $true; StopDenied = $false; CopyDenied = $false
        CopyFailures = 0; CorruptCopies = 0; CopyCount = 0; LaunchCount = 0
        Held = $false; LockAvailable = $true; ExitOnStart = $false
        ElevatedCount = 0; CancelUac = $false; ChildExitCode = $null; SpawnOnCopyFailure = $false
        ExeMissing = $false
    }
    [IO.File]::WriteAllText($srcPath, "[Settings]`r`nSharedMemorySupport=1`r`n")
    [IO.File]::WriteAllText($dstPath, "[Settings]`r`nSharedMemorySupport=0`r`n")
    if ($running) { [void]$state.Running.Add((New-TestProcess 101)) }
}
function Resolve-HWiNFO {
    [void]$script:state.Events.Add('resolve')
    if (-not $script:state.ExeMissing) { return $script:exePath }
}
function Get-Process {
    [CmdletBinding()]
    param([string]$Name, [int]$Id)
    if ($PSBoundParameters.ContainsKey('Id')) { return @($script:state.Running | Where-Object { $_.Id -eq $Id }) }
    return @($script:state.Running)
}
function Stop-Process {
    [CmdletBinding()]
    param($InputObject, [switch]$Force)
    [void]$script:state.Events.Add('stop')
    if ($script:state.StopDenied) { throw [UnauthorizedAccessException]::new('mock process access denied') }
    $script:state.Healthy = $false
    # Do not remove the process until WaitForExit: catch copy-before-exit bugs.
}
function Start-Sleep {
    param([int]$Milliseconds)
    [void]$script:state.Events.Add('sleep-' + $Milliseconds)
}
function Test-HWiNFOSharedMemory {
    if ($script:state.LaunchCount -gt 0) {
        $script:state.HealthPolls++
        return $script:state.HealthyAfterStart -and ($script:state.HealthPolls -gt $script:state.HealthDelay)
    }
    return $script:state.Healthy
}
function Get-HWiNFOConfigHash([string]$path) {
    if ($path -eq $script:dstPath) { [void]$script:state.Events.Add('verify') }
    return (& $script:ProductionHash $path)
}
function Copy-Item {
    [CmdletBinding()]
    param([string]$LiteralPath, [string]$Destination, [switch]$Force)
    Assert ($script:state.Running.Count -eq 0) 'INI overwrite attempted while HWiNFO is running'
    Assert $script:state.Held 'INI copy attempted outside transaction mutex'
    [void]$script:state.Events.Add('copy')
    $script:state.CopyCount++
    if ($script:state.CopyDenied) { throw [UnauthorizedAccessException]::new('mock INI access denied') }
    if ($script:state.CopyFailures -gt 0) {
        $script:state.CopyFailures--
        if ($script:state.SpawnOnCopyFailure) { [void]$script:state.Running.Add((New-TestProcess 150)) }
        throw [IO.IOException]::new('mock temporary file lock')
    }
    Microsoft.PowerShell.Management\Copy-Item -LiteralPath $LiteralPath -Destination $Destination -Force
    if ($script:state.CorruptCopies -gt 0) {
        $script:state.CorruptCopies--
        [IO.File]::WriteAllText($Destination, 'mock incomplete copy')
    }
}
function New-Object {
    [CmdletBinding()]
    param([string]$TypeName, [object[]]$ArgumentList)
    if ($TypeName -eq 'System.Threading.Mutex') {
        [void]$script:state.Events.Add('mutex-create')
        $mutex = [pscustomobject]@{}
        $mutex | Add-Member -MemberType ScriptMethod -Name WaitOne -Value {
            param($milliseconds)
            [void]$script:state.Events.Add('lock-' + $milliseconds)
            if (-not $script:state.LockAvailable -or $script:state.Held) { return $false }
            $script:state.Held = $true
            return $true
        }
        $mutex | Add-Member -MemberType ScriptMethod -Name ReleaseMutex -Value {
            Assert $script:state.Held 'Mutex released without ownership'
            $script:state.Held = $false
            [void]$script:state.Events.Add('unlock')
        }
        $mutex | Add-Member -MemberType ScriptMethod -Name Dispose -Value { [void]$script:state.Events.Add('mutex-dispose') }
        return $mutex
    }
    if ($PSBoundParameters.ContainsKey('ArgumentList')) {
        return ,(Microsoft.PowerShell.Utility\New-Object -TypeName $TypeName -ArgumentList $ArgumentList)
    }
    return ,(Microsoft.PowerShell.Utility\New-Object -TypeName $TypeName)
}
function Start-Process {
    [CmdletBinding()]
    param([string]$FilePath, [string]$WorkingDirectory, [string]$WindowStyle, [string]$Verb,
          [string]$ArgumentList, [switch]$PassThru, [switch]$Wait)
    if ($Verb -eq 'RunAs') {
        Assert (-not $script:state.Held) 'Parent held recovery mutex while waiting for elevated child'
        Assert ($WindowStyle -eq 'Hidden' -and $Wait) 'Elevated helper must be hidden and awaited'
        Assert ($ArgumentList -match '-File "[^"]+" -Mode restart -Elevated$') 'Elevated script path is not quoted/full transaction is not restarted'
        [void]$script:state.Events.Add('elevate')
        $script:state.ElevatedCount++
        if ($script:state.CancelUac) { throw [ComponentModel.Win32Exception]::new(1223) }
        if ($null -ne $script:state.ChildExitCode) { return [pscustomobject]@{ ExitCode = $script:state.ChildExitCode } }
        $oldMode = $script:Mode
        $script:Mode = 'restart'
        $script:Elevated = $true
        $script:state.StopDenied = $false
        $script:state.CopyDenied = $false
        try { $rc = Invoke-HWiNFORecoveryEntry }
        finally { $script:Elevated = $false; $script:Mode = $oldMode }
        return [pscustomobject]@{ ExitCode = $rc }
    }
    Assert $script:state.Held 'HWiNFO launched outside transaction mutex'
    Assert ($script:state.Running.Count -eq 0) 'Launched a duplicate HWiNFO process'
    Assert ((& $script:ProductionHash $script:srcPath) -eq (& $script:ProductionHash $script:dstPath)) 'Launched before INI copy/verification completed'
    Assert ($FilePath -eq $script:exePath -and $WorkingDirectory -eq $script:installRoot) 'Wrong executable or working directory'
    Assert (-not $ArgumentList) 'HWiNFO invocation added command-line switches'
    [void]$script:state.Events.Add('launch')
    $script:state.LaunchCount++
    $process = New-TestProcess (200 + $script:state.LaunchCount)
    if (-not $script:state.ExitOnStart) { [void]$script:state.Running.Add($process) }
    return $process
}

$script:passed = 0
function Test-Case([string]$name, [scriptblock]$body) {
    Reset-Test
    & $body
    Assert (-not $script:state.Held) 'Transaction left its mutex held'
    $script:passed++
    Write-Output ('PASS ' + $name)
}

try {
    Test-Case 'healthy start does not overwrite a live INI' {
        $script:Mode = 'start'; $state.Healthy = $true
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Healthy start failed'
        Assert ($state.CopyCount -eq 0 -and $state.LaunchCount -eq 0 -and -not $state.Events.Contains('stop')) 'Healthy start mutated HWiNFO'
    }
    Test-Case 'running without shared memory: exit -> copy -> verify -> launch' {
        $script:Mode = 'start'
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Recovery failed'
        Assert-Order @('stop', 'exit-confirmed', 'copy', 'verify', 'launch', 'unlock')
        Assert ($state.CopyCount -eq 1 -and $state.LaunchCount -eq 1) 'Unexpected extra recovery'
    }
    Test-Case 'explicit restart restores configuration even when previously healthy' {
        $state.Healthy = $true
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Restart failed'
        Assert-Order @('stop', 'exit-confirmed', 'copy', 'verify', 'launch')
    }
    Test-Case 'cold start copies and verifies before launch' {
        Reset-Test $false; $script:Mode = 'start'
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Cold start failed'
        Assert-Order @('copy', 'verify', 'launch')
    }
    Test-Case 'unconfirmed process exit prevents copy and launch' {
        $state.WaitResult = $false
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 4) 'Expected exit failure'
        Assert ($state.CopyCount -eq 0 -and $state.LaunchCount -eq 0) 'Exit failure still copied/launched'
    }
    Test-Case 'process access denied releases parent mutex before full elevated retry' {
        $state.StopDenied = $true
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Elevated stop retry failed'
        Assert-Order @('stop', 'unlock', 'mutex-dispose', 'elevate', 'lock-10000', 'stop', 'exit-confirmed', 'copy', 'verify', 'launch')
    }
    Test-Case 'INI access denied releases parent mutex before full elevated retry' {
        $state.CopyDenied = $true
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Elevated copy retry failed'
        Assert-Order @('exit-confirmed', 'copy', 'unlock', 'mutex-dispose', 'elevate', 'lock-10000', 'copy', 'verify', 'launch')
        Assert ($state.ElevatedCount -eq 1) 'Elevation did not happen exactly once'
    }
    Test-Case 'already elevated copy failure does not recursively elevate' {
        $script:Elevated = $true; $state.CopyDenied = $true
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 5) 'Expected copy failure'
        Assert ($state.ElevatedCount -eq 0 -and $state.LaunchCount -eq 0) 'Recursive elevation/launch'
    }
    Test-Case 'already elevated stop failure does not recursively elevate' {
        $script:Elevated = $true; $state.StopDenied = $true
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 4) 'Expected stop failure'
        Assert ($state.ElevatedCount -eq 0 -and $state.CopyCount -eq 0) 'Recursive elevation/copy'
    }
    Test-Case 'temporary INI file lock retries while process remains stopped' {
        $state.CopyFailures = 2
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'File lock retry failed'
        Assert ($state.CopyCount -eq 3 -and $state.LaunchCount -eq 1) 'Incorrect retry count'
        Assert-Order @('exit-confirmed', 'copy', 'sleep-200', 'copy', 'sleep-200', 'copy', 'verify', 'launch')
    }
    Test-Case 'permanent INI file lock is bounded and never launches' {
        $state.CopyFailures = 10
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 5) 'Expected bounded copy failure'
        Assert ($state.CopyCount -eq 5 -and $state.LaunchCount -eq 0 -and $state.ElevatedCount -eq 0) 'Copy retries unbounded/launched/elevated unnecessarily'
    }
    Test-Case 'incorrect copied bytes are retried and verified before launch' {
        $state.CorruptCopies = 2
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Verification retry failed'
        Assert ($state.CopyCount -eq 3 -and $state.LaunchCount -eq 1) 'Bad bytes were not retried'
    }
    Test-Case 'persistent incorrect bytes prevent launch' {
        $state.CorruptCopies = 10
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 5) 'Expected verification failure'
        Assert ($state.CopyCount -eq 5 -and $state.LaunchCount -eq 0) 'Launched with wrong config'
    }
    Test-Case 'new live instance blocks subsequent copy retry' {
        $state.CopyFailures = 1; $state.SpawnOnCopyFailure = $true
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 4) 'Expected new-process guard failure'
        Assert ($state.CopyCount -eq 1 -and $state.LaunchCount -eq 0) 'Copied over new live instance'
    }
    Test-Case 'process exiting during initialization is not reported healthy' {
        $state.ExitOnStart = $true
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 6) 'Expected early process exit'
    }
    Test-Case 'missing shared memory times out; next retry repeats full transaction' {
        $state.HealthyAfterStart = $false
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 7) 'Expected shared memory timeout'
        Assert ($state.HealthPolls -eq 120) 'Health wait changed unexpectedly'
        $state.HealthyAfterStart = $true
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Second recovery failed'
        Assert-Order @('launch', 'unlock', 'mutex-create', 'stop', 'exit-confirmed', 'copy', 'verify', 'launch')
        Assert ($state.LaunchCount -eq 2 -and $state.CopyCount -eq 2) 'Health retry skipped stop/copy'
    }
    Test-Case 'slow shared-memory initialization waits instead of restarting repeatedly' {
        $state.HealthDelay = 5
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Slow initialization failed'
        Assert ($state.HealthPolls -eq 6 -and $state.LaunchCount -eq 1) 'Initialization restarted prematurely'
    }
    Test-Case 'mutex contention is a no-op with code 11' {
        $state.LockAvailable = $false
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 11) 'Expected busy code'
        Assert ($state.CopyCount -eq 0 -and $state.LaunchCount -eq 0 -and -not $state.Events.Contains('stop')) 'Busy recovery mutated state'
        Assert (-not $state.Events.Contains('unlock')) 'Released someone else''s mutex'
    }
    Test-Case 'missing template is detected before stopping HWiNFO' {
        Microsoft.PowerShell.Management\Remove-Item -LiteralPath $srcPath
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 3) 'Expected missing template'
        Assert (-not $state.Events.Contains('stop')) 'Stopped HWiNFO without a repair template'
    }
    Test-Case 'missing executable is explicit and does not copy' {
        $state.ExeMissing = $true
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 2) 'Expected missing executable'
        Assert ($state.CopyCount -eq 0) 'Copied without executable'
    }
    Test-Case 'copy helper independently refuses a live process' {
        try { Copy-HWiNFOConfig -Source $srcPath -Destination $dstPath; throw 'Expected a copy guard failure' }
        catch { Assert ($_.Exception.Data['HWiNFOExitCode'] -eq 4) 'Wrong live-copy failure' }
        Assert ($state.CopyCount -eq 0) 'Live copy guard failed'
    }
    Test-Case 'canceled UAC preserves original failure and does not launch' {
        $state.CopyDenied = $true; $state.CancelUac = $true
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 5) 'UAC cancellation lost original failure'
        Assert ($state.LaunchCount -eq 0 -and $state.ElevatedCount -eq 1) 'UAC cancellation launched/looped'
    }
    Test-Case 'elevated child failure is returned to caller' {
        $state.CopyDenied = $true; $state.ChildExitCode = 7
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 7) 'Child failure was not propagated'
    }
    Test-Case 'all running instances are confirmed exited before copying' {
        [void]$state.Running.Add((New-TestProcess 102))
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Multiple process recovery failed'
        Assert (($state.Events | Where-Object { $_ -eq 'exit-confirmed' }).Count -eq 2) 'Did not wait for every instance'
        Assert-Order @('stop', 'stop', 'exit-confirmed', 'exit-confirmed', 'copy', 'verify', 'launch')
    }
    # Validate real SM2 parsing against isolated named mappings. Only names are
    # substituted; production offsets, bounds checks, reads and disposal execute.
    $prefix = 'YMCC_HWINFO_TEST_' + [Guid]::NewGuid().ToString('N')
    $body = $script:SharedMemorySource.Substring(1, $script:SharedMemorySource.Length - 2)
    $body = $body.Replace('Global\HWiNFO_SENS_SM2', ($prefix + '_0')).Replace('HWiNFO_SENS_SM2', ($prefix + '_1')).Replace('Global\HWiNFO_SENS_SM', ($prefix + '_2')).Replace('HWiNFO_SENS_SM', ($prefix + '_3'))
    $realSharedMemoryProbe = [scriptblock]::Create($body)
    Test-Case 'real shared-memory probe rejects missing mapping' {
        Assert (-not (& $realSharedMemoryProbe)) 'Missing mapping passed health check'
    }
    $mapping = [IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew(($prefix + '_1'), 512)
    $view = $mapping.CreateViewAccessor()
    try {
        Test-Case 'real shared-memory probe rejects invalid signature' {
            Assert (-not (& $realSharedMemoryProbe)) 'Invalid signature passed health check'
        }
        $view.Write(0, [uint32]0x53695748); $view.Write(32, [uint32]44); $view.Write(36, [uint32]316); $view.Write(40, [uint32]1)
        Test-Case 'real shared-memory probe accepts a readable SM2 reading region' {
            Assert ([bool](& $realSharedMemoryProbe)) 'Valid SM2 mapping rejected'
        }
        $view.Write(40, [uint32]0)
        Test-Case 'real shared-memory probe rejects zero readings' {
            Assert (-not (& $realSharedMemoryProbe)) 'Zero readings passed health check'
        }
        $view.Write(40, [uint32]([Math]::Floor(($view.Capacity - 44) / 316) + 1))
        Test-Case 'real shared-memory probe rejects out-of-bounds readings' {
            Assert (-not (& $realSharedMemoryProbe)) 'Out-of-bounds readings passed health check'
        }
    } finally { $view.Dispose(); $mapping.Dispose() }
    Write-Output ("HWINFO_RECOVERY_PASS ({0} cases; actual production functions; no real HWiNFO/process/UAC operations)" -f $script:passed)
} finally {
    # One shell, literal paths, and an absolute temp-root check before recursion.
    $resolvedRoot = [IO.Path]::GetFullPath($script:tempRoot)
    $tempParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolvedRoot.StartsWith($tempParent, [StringComparison]::OrdinalIgnoreCase) -or
        -not ([IO.Path]::GetFileName($resolvedRoot)).StartsWith('YMCC HWiNFO Recovery Test ')) {
        throw 'Refusing to remove a test directory outside the intended temp workspace'
    }
    Microsoft.PowerShell.Management\Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
}
