# Closed normal-exit controller. No force switch, arbitrary CLI, service/task or SYSTEM route.
function Get-RogOwnedTree($Options,$RootRow) {
    $queue=New-Object 'System.Collections.Generic.Queue[object]'; $queue.Enqueue($RootRow)
    $seen=@{}; $safe=@(); $exeDir=[IO.Path]::GetDirectoryName($Options.ExePath).TrimEnd('\')+'\'
    $edgeRuntimeRoots=@(); foreach ($pf in @($env:ProgramFiles,${env:ProgramFiles(x86)})) { if ($pf) { $edgeRuntimeRoots+=([IO.Path]::GetFullPath((Join-Path $pf 'Microsoft\EdgeWebView\Application')).TrimEnd('\')+'\') } }
    $systemRoots=@(); foreach ($sr in @((Join-Path $env:SystemRoot 'System32'),(Join-Path $env:SystemRoot 'SysWOW64'))) { if ($sr) { $systemRoots+=([IO.Path]::GetFullPath($sr).TrimEnd('\')+'\') } }
    while ($queue.Count -gt 0) {
        $parent=$queue.Dequeue(); $id=[int]$parent.ProcessId
        if ($seen.ContainsKey($id)) { continue }; $seen[$id]=$true
        $safe+=@{pid=$id;creationTimeUtc=Format-RogUtc $parent.CreationDate;exePath=[IO.Path]::GetFullPath($parent.ExecutablePath)}
        try { $children=@(Get-CimInstance Win32_Process -Filter ('ParentProcessId='+$id) -ErrorAction Stop) }
        catch { throw 'OWNED_TREE_QUERY_PERMISSION_FAILED' }
        foreach ($child in $children) {
            if (-not $child.ExecutablePath) { throw 'OWNED_TREE_PATH_PERMISSION_FAILED' }
            $path=[IO.Path]::GetFullPath($child.ExecutablePath)
            $fileName=[IO.Path]::GetFileName($path)
            $insideTarget=$path.StartsWith($exeDir,[StringComparison]::OrdinalIgnoreCase)
            $isOwnedWebView2=($fileName -ieq 'msedgewebview2.exe') -and (@($edgeRuntimeRoots | Where-Object { $path.StartsWith($_,[StringComparison]::OrdinalIgnoreCase) }).Count -gt 0)
            $isOwnedConsoleHost=($fileName -ieq 'conhost.exe') -and (@($systemRoots | Where-Object { $path.StartsWith($_,[StringComparison]::OrdinalIgnoreCase) }).Count -gt 0)
            $isOwnedTdpDaemon=($fileName -ieq 'YeManTdpCtl.exe')
            if (-not $insideTarget -and -not $isOwnedWebView2 -and -not $isOwnedTdpDaemon -and -not $isOwnedConsoleHost) { throw 'OWNED_TREE_OUTSIDE_TARGET_NAMESPACE' }
            Assert-RogNoReparse $path
            if ((Get-RogOwnerSid $child) -ne (Get-RogPrincipalSid) -or
                (Convert-RogUtc $child.CreationDate) -lt (Convert-RogUtc $parent.CreationDate)) { throw 'OWNED_TREE_IDENTITY_UNVERIFIED' }
            $queue.Enqueue($child)
        }
        if ($seen.Count -gt 256) { throw 'OWNED_TREE_SIZE_LIMIT' }
    }
    return $safe
}
function Get-RogProcessHandle([int]$ProcessId) {
    try { $p=Get-Process -Id $ProcessId -ErrorAction Stop; $handle=$p.Handle; return $p }
    catch { throw 'PROCESS_HANDLE_PERMISSION_FAILED' }
}
function Invoke-RogNormalClose($Handle) {
    # The product's only normal-exit channel is its own WM_APP_EXIT (WM_USER+6 = 0x0406),
    # exactly what the tray "exit" menu item and the frontend app.exit IPC post. WM_CLOSE is
    # deliberately not used: while the tray/taskbar resident mode is active the product maps
    # WM_CLOSE to hide-to-tray and never terminates. Still a graceful message to the exact
    # verified PID's own top-level window(s); no force, no kill-by-name.
    if (-not ('RogUser32Window' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class RogUser32Window {
    private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    public const uint WM_APP_EXIT = 0x0406; // WM_USER + 6
    public static int PostExitToProcess(int pid) {
        int posted = 0;
        EnumWindows(delegate(IntPtr hWnd, IntPtr lParam) {
            uint wp; GetWindowThreadProcessId(hWnd, out wp);
            if (wp == (uint)pid && PostMessage(hWnd, WM_APP_EXIT, IntPtr.Zero, IntPtr.Zero)) { posted++; }
            return true;
        }, IntPtr.Zero);
        return posted;
    }
}
'@ -ErrorAction Stop
    }
    try { return ([RogUser32Window]::PostExitToProcess([int]$Handle.Id) -gt 0) } catch { throw 'NORMAL_CLOSE_PERMISSION_FAILED' }
}
function Start-RogExactTarget($Options) {
    # A finite public session CLI only; process-local environment intentionally inherited.
    $a=@{FilePath=$Options.ExePath;WorkingDirectory=[IO.Path]::GetDirectoryName($Options.ExePath);PassThru=$true;ErrorAction='Stop'}
    if ($Options.StartArgument.Count) { $a.ArgumentList=($Options.StartArgument -join ' ') }
    try { return Start-Process @a } catch { throw 'TARGET_START_PERMISSION_OR_POLICY_FAILED' }
}
function Stop-RogExactTarget($Options) {
    [void](Assert-RogTarget $Options -RequireTrusted)
    $row=Assert-RogProcess $Options
    $tree=@(Get-RogOwnedTree $Options $row)
    $handle=Get-RogProcessHandle $Options.TargetPid
    try {
        # Holding the exact process handle prevents silently targeting a reused PID.
        [void](Assert-RogProcess $Options); [void](Assert-RogTarget $Options -RequireTrusted)
        if (-not (Invoke-RogNormalClose $handle)) { throw 'NORMAL_CLOSE_UNAVAILABLE_NO_FORCE' }
        $deadline=(Get-RogClock).AddSeconds($Options.GraceSeconds)
        do {
            $remaining=@()
            foreach ($identity in $tree) {
                $now=Get-RogProcessRow $identity.pid
                if ($null -eq $now) { continue }
                if (-not $now.ExecutablePath) { throw 'OWNED_TREE_PATH_PERMISSION_FAILED' }
                if ([IO.Path]::GetFullPath($now.ExecutablePath) -ine $identity.exePath -or
                    (Convert-RogUtc $now.CreationDate).Ticks -ne (Convert-RogUtc $identity.creationTimeUtc).Ticks -or
                    (Get-RogOwnerSid $now) -ne (Get-RogPrincipalSid)) { throw 'OWNED_TREE_PROCESS_IDENTITY_CHANGED' }
                $remaining+=@($identity)
            }
            if (-not $remaining.Count) { return @{normalCloseRequested=$true;ownedTreeExited=$true;forceUsed=$false;targetPid=$Options.TargetPid;targetCreationTimeUtc=$Options.TargetCreationTimeUtc;trackedProcessCount=$tree.Count} }
            # If the root is alive, capture new owned children without broad process enumeration.
            $rootNow=Get-RogProcessRow $Options.TargetPid
            if ($rootNow) {
                [void](Assert-RogProcess $Options)
                foreach ($fresh in @(Get-RogOwnedTree $Options $rootNow)) {
                    if (@($tree | Where-Object {$_.pid -eq $fresh.pid}).Count -eq 0) { $tree+=@($fresh) }
                }
            }
            if ((Get-RogClock) -ge $deadline) { break }; Wait-RogTick
        } while ($true)
        throw 'NORMAL_EXIT_TIMEOUT_OWNED_TREE_NOT_CLOSED_NO_FORCE'
    } finally { $handle.Dispose() }
}
function Get-RogWindowObserved($Process) { try { $Process.Refresh(); return ($Process.MainWindowHandle -ne [IntPtr]::Zero) } catch { return $false } }
function Start-RogVerifiedTarget($Options) {
    $state=Get-RogStatus $Options
    if ($state.rootCount -ne 0) { throw 'TARGET_ALREADY_RUNNING_OR_AMBIGUOUS' }
    [void](Assert-RogTarget $Options -RequireTrusted)
    $process=Start-RogExactTarget $Options
    $handle=$process.Handle
    try {
        $deadline=(Get-RogClock).AddSeconds($Options.StartWaitSeconds); $row=$null
        do {
            $row=Get-RogProcessRow $process.Id
            if ($row) { break }
            if ((Get-RogClock) -ge $deadline) { throw 'TARGET_START_NOT_CONFIRMED' }; Wait-RogTick
        } while ($true)
        $birth=Format-RogUtc $row.CreationDate
        [void](Assert-RogProcess $Options $process.Id $birth)
        [void](Assert-RogTarget $Options -RequireTrusted)
        $targetPrivilege=Get-RogTargetPrivilege $process.Id
        if (-not $targetPrivilege.canMutateYmcc) { throw 'TARGET_HIGH_TOKEN_UNVERIFIED' }
        return @{targetPid=[int]$process.Id;targetCreationTimeUtc=$birth;exePath=$Options.ExePath;currentUserVerified=$true;
            targetTokenEvidence=$targetPrivilege;targetPrivilegeVerified=$true;processAliveObserved=$true;windowObserved=(Get-RogWindowObserved $process);frontendReadyVerified=$false;runtimeReadyVerified=$false;startArgumentCount=$Options.StartArgument.Count;rawArgumentsRecorded=$false;
            environmentInherited=$true;environmentRecorded=$false;rogArgumentEnvironmentVerified=$false}
    } finally { $process.Dispose() }
}
function Invoke-RogLifecycle($Options,$Context) {
    if ($Options.Action -notin @('status','start','stop','restart')) { throw 'LIFECYCLE_ACTION_NOT_ALLOWED' }
    if ($Options.Action -eq 'status') { return Get-RogStatus $Options }
    Assert-RogAuthorized $Options; [void](Assert-RogTarget $Options -RequireTrusted)
    $privilege=Get-RogPrivilege
    if (-not $privilege.verifiedHigh) { throw 'PRIVILEGE_REQUIRED_NOT_AUTHORIZED' }
    Write-RogLifecycleRecord $Context $Options @{action='privilege.acquire';privilege=@{canMutateYmcc=$true;verifiedHigh=$true};applied=$true}
    try {
        if ($Options.Action -eq 'start') {
            $started=Start-RogVerifiedTarget $Options
            Write-RogLifecycleRecord $Context $Options @{action='ymcc.start';applied=$true;targetPid=$started.targetPid;targetCreationTimeUtc=$started.targetCreationTimeUtc;targetPrivilegeVerified=$started.targetPrivilegeVerified;targetTokenEvidence=$started.targetTokenEvidence}
            return $started
        }
        if (-not $Options.TargetPid -or -not $Options.TargetCreationTimeUtc) { throw 'EXACT_PROCESS_IDENTITY_REQUIRED' }
        $stopped=Stop-RogExactTarget $Options
        Write-RogLifecycleRecord $Context $Options @{action='ymcc.stop';applied=$true;rootStopped=$true;cleanupComplete=$true;forceUsed=$false;targetPid=$Options.TargetPid;targetCreationTimeUtc=$Options.TargetCreationTimeUtc}
        if ($Options.Action -eq 'stop') { return $stopped }
        $start=@{}; foreach ($key in $Options.Keys) { $start[$key]=$Options[$key] }
        $start.TargetPid=0; $start.TargetCreationTimeUtc=''
        $started=Start-RogVerifiedTarget $start
        Write-RogLifecycleRecord $Context $Options @{action='ymcc.start';applied=$true;targetPid=$started.targetPid;targetCreationTimeUtc=$started.targetCreationTimeUtc;targetPrivilegeVerified=$started.targetPrivilegeVerified;targetTokenEvidence=$started.targetTokenEvidence}
        Write-RogLifecycleRecord $Context $Options @{action='ymcc.restart';applied=$true;stopApplied=$true;startApplied=$true;targetPid=$started.targetPid;targetCreationTimeUtc=$started.targetCreationTimeUtc}
        return @{stop=$stopped;start=$started;windowObserved=$started.windowObserved;frontendReadyVerified=$false;runtimeReadyVerified=$false;lifecycleEvidenceRelativePath='lifecycle-control.jsonl'}
    } catch {
        Write-RogLifecycleRecord $Context $Options @{action=('ymcc.'+$Options.Action);applied=$false;errorCode=(Get-RogCode $_);forceUsed=$false}
        throw
    }
}
if ($MyInvocation.InvocationName -ne '.') {
    $raw=@($args)
    . (Join-Path $PSScriptRoot 'Invoke-ROG-CPU.ps1')
    $outcome=@(Invoke-RogEntry @{} $PSScriptRoot $raw 'lifecycle')
    $code=[int]$outcome[-1]; if ($outcome.Count -gt 1) { $outcome[0..($outcome.Count-2)] | Write-Output }; exit $code
}