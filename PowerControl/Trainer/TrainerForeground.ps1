# External trainers remain unmodified. Decky runs in foreground Steam while
# YMCC is often in the tray; the interactive desktop Shell owns this one launch.
# Keep the normal YMCC menu on its original Process.Start path. Never retry a
# ShellExecute that may already have started the program, even if no PID returns.
function Initialize-TrainerInteractiveNative {
    if('YmccTrainerInteractiveNative' -as [type]){return}
    Add-Type -TypeDefinition @"
using System;using System.Text;using System.Runtime.InteropServices;
public static class YmccTrainerInteractiveNative {
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern IntPtr GetShellWindow();
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetClassName(IntPtr w,StringBuilder name,int count);
 [DllImport("user32.dll")]public static extern bool IsIconic(IntPtr w);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr w,out uint pid);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern IntPtr CreateFile(string p,uint access,uint share,IntPtr security,uint disposition,uint flags,IntPtr template);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]static extern uint GetFinalPathNameByHandle(IntPtr h,StringBuilder p,uint size,uint flags);
 [DllImport("kernel32.dll")]static extern bool CloseHandle(IntPtr h);
 public static string FinalPath(string path){var h=CreateFile(path,0,7,IntPtr.Zero,3,0,IntPtr.Zero);if(h==new IntPtr(-1))return null;try{var b=new StringBuilder(32768);uint n=GetFinalPathNameByHandle(h,b,32768,0);return n>0&&n<32768?b.ToString():null;}finally{CloseHandle(h);}}
}
"@
}
function Test-TrainerSteamForeground([string]$AppDirectory) {
    # Backward compatibility for already-installed frontends: their existing
    # request includes AppDirectory but not the new InteractiveShell switch.
    # This read does not start Steam, register a URI, or change any setting.
    if(-not $AppDirectory -or -not [IO.File]::Exists((Join-Path $AppDirectory 'YeManCC.exe'))){return $false}
    try{
        Initialize-TrainerInteractiveNative
        $owner=[uint32]0
        [YmccTrainerInteractiveNative]::GetWindowThreadProcessId([YmccTrainerInteractiveNative]::GetForegroundWindow(),[ref]$owner)|Out-Null
        $process=Get-Process -Id $owner -ErrorAction Stop
        if($process.ProcessName -notin @('steam','steamwebhelper') -or $process.SessionId -ne [Diagnostics.Process]::GetCurrentProcess().SessionId){return $false}
        $key=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\Valve\Steam')
        if(-not $key){return $false}
        try{$steamRoot=[string]$key.GetValue('SteamPath','')}finally{$key.Dispose()}
        if(-not $steamRoot){return $false}
        $prefix=[IO.Path]::GetFullPath($steamRoot).TrimEnd('\')+'\'
        return $process.Path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)
    }catch{return $false}
}
function Get-TrainerLaunchOwnerState([string]$AppDirectory) {
    if(-not $AppDirectory){return $null}
    $expected=Join-Path $AppDirectory 'YeManCC.exe'
    if(-not [IO.File]::Exists($expected)){return $null}
    Initialize-TrainerInteractiveNative
    $foregroundPid=[uint32]0
    [YmccTrainerInteractiveNative]::GetWindowThreadProcessId([YmccTrainerInteractiveNative]::GetForegroundWindow(),[ref]$foregroundPid)|Out-Null
    $foreground=Get-Process -Id $foregroundPid -ErrorAction SilentlyContinue
    if($foreground -and $foreground.Path -ieq $expected){return @{found=$true;visible=$true}}
    # Only a direct child of this exact YMCC image can use the hidden-owner
    # compatibility path. Game overlays need not make Steam the OS foreground.
    $self=Get-CimInstance Win32_Process -Filter ('ProcessId='+$PID) -OperationTimeoutSec 2 -ErrorAction Stop
    $parent=Get-Process -Id $self.ParentProcessId -ErrorAction Stop
    if($parent.Path -ine $expected -or $parent.SessionId -ne [Diagnostics.Process]::GetCurrentProcess().SessionId){return $null}
    Initialize-TrainerLaunchNative
    $windows=[Collections.Generic.List[bool]]::new();$ownerId=[uint32]$parent.Id
    $callback=[YmccTrainerLaunchNative+EnumProc]{param($window,$unused)
        $owner=[uint32]0;[YmccTrainerLaunchNative]::GetWindowThreadProcessId($window,[ref]$owner)|Out-Null
        if($owner -eq $ownerId){$name=New-Object Text.StringBuilder 128;[YmccTrainerInteractiveNative]::GetClassName($window,$name,128)|Out-Null
            if($name.ToString() -ceq 'QQ'){$windows.Add(([YmccTrainerLaunchNative]::IsWindowVisible($window) -and -not [YmccTrainerInteractiveNative]::IsIconic($window)))}}
        return $true
    }
    [YmccTrainerLaunchNative]::EnumWindows($callback,[IntPtr]::Zero)|Out-Null
    if(-not $windows.Count){return $null}
    return @{found=$true;visible=($windows.Contains($true))}
}
function Test-TrainerBackgroundOwner([string]$AppDirectory) {
    try{$owner=Get-TrainerLaunchOwnerState $AppDirectory;return $owner -and $owner.found -eq $true -and $owner.visible -eq $false}catch{return $false}
}
function Get-TrainerDesktopShell {
    Initialize-TrainerInteractiveNative
    $expectedWindow=[YmccTrainerInteractiveNative]::GetShellWindow()
    if($expectedWindow -eq [IntPtr]::Zero){throw 'Windows 桌面 Shell 尚不可用'}
    $root=New-Object -ComObject Shell.Application
    $windows=$root.Windows();$handle=0
    # Do not execute through the in-process Shell.Application object: retrieve
    # the desktop view's Application, which lives in the interactive Shell.
    $desktop=$windows.FindWindowSW(0,0,8,[ref]$handle,1)
    if(-not $desktop -or [IntPtr]$handle -ne $expectedWindow){throw 'Windows 桌面 Shell 身份不匹配'}
    $owner=[uint32]0
    [YmccTrainerInteractiveNative]::GetWindowThreadProcessId($expectedWindow,[ref]$owner)|Out-Null
    $process=Get-Process -Id $owner -ErrorAction Stop
    $expected=Join-Path ([Environment]::GetFolderPath('Windows')) 'explorer.exe'
    if($process.Path -ine $expected -or $process.SessionId -ne [Diagnostics.Process]::GetCurrentProcess().SessionId){throw 'Windows 桌面 Shell 会话不匹配'}
    return $desktop.Document.Application
}
function Close-TrainerDesktopShell($Shell) {
    if($null -ne $Shell -and [Runtime.InteropServices.Marshal]::IsComObject($Shell)){[Runtime.InteropServices.Marshal]::FinalReleaseComObject($Shell)|Out-Null}
}
function Find-TrainerShellProcess([string]$Program,[DateTime]$NotBefore) {
    Initialize-TrainerInteractiveNative
    $expected=[YmccTrainerInteractiveNative]::FinalPath($Program)
    if(-not $expected){return $null}
    # A compatibility path may use an NTFS short executable name. Observe both
    # the selected spelling and the final file's name, then compare final paths.
    $names=@([IO.Path]::GetFileNameWithoutExtension($Program),[IO.Path]::GetFileNameWithoutExtension($expected))|Select-Object -Unique
    $candidates=@(foreach($name in $names){[Diagnostics.Process]::GetProcessesByName($name)})
    $matches=@(foreach($candidate in $candidates){
        try{
            if($candidate.StartTime.ToUniversalTime() -ge $NotBefore -and -not $candidate.HasExited){
                $actual=[YmccTrainerInteractiveNative]::FinalPath($candidate.MainModule.FileName)
                if($actual -and $actual.Equals($expected,[StringComparison]::OrdinalIgnoreCase)){$candidate}
            }
        }catch{}
    })
    if($matches.Count -gt 1){throw '交互启动后发现多个同文件进程，启动结果未确认，不会重复启动'}
    if($matches.Count -eq 1){return $matches[0]}
    return $null
}
function Find-TrainerCanonicalRunningProcess([string]$Program) {
    # safePath/junction spelling is not a second program. Reuse the same final
    # file before dispatch; ambiguity refuses another launch.
    return Find-TrainerShellProcess $Program ([DateTime]::MinValue)
}
function Get-TrainerProcessForeground([uint32]$ProcessId) {
    Initialize-TrainerInteractiveNative
    $owner=[uint32]0
    [YmccTrainerInteractiveNative]::GetWindowThreadProcessId([YmccTrainerInteractiveNative]::GetForegroundWindow(),[ref]$owner)|Out-Null
    return $owner -eq $ProcessId
}
function Test-TrainerProcessWindowReady([uint32]$ProcessId) {
    Initialize-TrainerLaunchNative
    $found=[Collections.Generic.List[IntPtr]]::new()
    $callback=[YmccTrainerLaunchNative+EnumProc]{param($window,$unused)
        $owner=[uint32]0
        [YmccTrainerLaunchNative]::GetWindowThreadProcessId($window,[ref]$owner)|Out-Null
        if($owner -eq $ProcessId){$title=New-Object Text.StringBuilder 512;[YmccTrainerLaunchNative]::GetWindowText($window,$title,512)|Out-Null;if($title.Length){$found.Add($window)}}
        return $true
    }
    [YmccTrainerLaunchNative]::EnumWindows($callback,[IntPtr]::Zero)|Out-Null
    return $found.Count -gt 0
}
function Invoke-TrainerShellPlan($Plan,$Shell) {
    $boundary=[DateTime]::UtcNow
    # Commit exactly once. COM has no PID return: a timeout/error after this call
    # is uncertain, never permission to fall through to Process.Start.
    try{$Shell.ShellExecute($Plan.program,$Plan.arguments,$Plan.workingDirectory,$Plan.verb,1)}
    catch{throw ('Windows 交互启动未确认，不会重复启动：'+$_.Exception.Message)}
    $process=$null
    for($attempt=0;$attempt -lt 25;$attempt++){
        $process=Find-TrainerShellProcess $Plan.program $boundary
        if($process){break}
        Start-Sleep -Milliseconds 80
    }
    if(-not $process){throw 'Windows 已接收交互启动，但进程未确认；请检查权限确认或任务切换，不会重复启动'}
    $foreground=$false;$focusRequested=$false
    for($attempt=0;$attempt -lt 25;$attempt++){
        if(Get-TrainerProcessForeground ([uint32]$process.Id)){$foreground=$true;break}
        $process.Refresh();if($process.HasExited){break}
        if(-not $focusRequested -and (Test-TrainerProcessWindowReady ([uint32]$process.Id))){
            $focusRequested=$true
            # One standard restore request, never Alt/input injection, TOPMOST,
            # shared input queues, another process launch, or repeated focusing.
            try{Show-TrainerProcess $process}catch{}
        }
        Start-Sleep -Milliseconds 80
    }
    return @{reused=$false;pid=$process.Id;launchBackend='desktop-shell';focusRequested=$focusRequested;window=$(if($foreground){'foreground'}else{'unconfirmed'});
        notice=$(if($foreground){'修改器窗口已打开'}else{'修改器已启动，前台窗口尚未确认；请从任务切换查看，无需重复启动'})}
}
