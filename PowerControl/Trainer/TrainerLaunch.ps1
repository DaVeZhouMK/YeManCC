# Loaded by TrainerBackend. These functions run only after an explicit player launch.
# Load the optional Shell path beside its consumer so hot updates remain compatible
# with already-installed TrainerBackend/YeManTrainerLaunch entry versions.
. (Join-Path $PSScriptRoot 'TrainerForeground.ps1')
function Initialize-TrainerLaunchNative {
    if ('YmccTrainerLaunchNative' -as [type]) { return }
    Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class YmccTrainerLaunchNative {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder title, int count);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern uint GetShortPathName(string input, StringBuilder output, uint count);
}
"@
}
function Get-TrainerSafeLaunchPath([string]$Path) {
    if ($Path -notmatch '[^\x00-\x7f]') { return $Path }
    Initialize-TrainerLaunchNative
    $buffer = New-Object Text.StringBuilder 32768
    $size = [YmccTrainerLaunchNative]::GetShortPathName($Path, $buffer, 32768)
    if ($size -gt 0 -and $size -lt 32768 -and $buffer.ToString() -notmatch '[^\x00-\x7f]') { return $buffer.ToString() }
    # GCM uses the same directory-junction fallback when NTFS short names are disabled.
    $parent = [IO.Path]::GetDirectoryName($Path)
    $junction = Join-Path ([IO.Path]::GetTempPath()) ('YMCC_TrainerLaunch_' + (Get-TrainerHash $parent.ToLowerInvariant()).Substring(0,24))
    if (Test-Path -LiteralPath $junction) {
        $item = Get-Item -LiteralPath $junction -Force
        if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or @($item.Target).Count -ne 1 -or [IO.Path]::GetFullPath([string](@($item.Target)[0])).TrimEnd('\') -ine $parent.TrimEnd('\')) { throw '修改器兼容路径已被其他目录占用' }
    } else {
        # Windows PowerShell 5.1 resolves junction targets as wildcard paths; 7 uses literal targets.
        # GCM author folders such as [风灵] must survive that version-specific resolution unchanged.
        $junctionTarget = if ($PSVersionTable.PSVersion.Major -le 5) { [WildcardPattern]::Escape($parent) } else { $parent }
        New-Item -ItemType Junction -Path $junction -Target $junctionTarget -ErrorAction Stop | Out-Null
    }
    return Join-Path $junction ([IO.Path]::GetFileName($Path))
}
function Resolve-TrainerLaunchPlan {
    param([ValidateSet('Trainer','Manager','CheatEngine')][string]$Mode='Trainer', [string]$TrainerPath='', [string]$TrainerFolder='', [string]$ExpectedSha256='',
        [string]$GcmSettingsRoot=(Join-Path $env:APPDATA 'GCM Settings'), [string]$GcmDefaultRoot=(Join-Path $env:APPDATA 'GCM Trainers'),
        [string]$ManagerPath='', [string]$AppDirectory='', [string]$TrainerGameName='', [string]$TrainerOrigin='', [string]$GcmResourceId='')
    $settings = Repair-TrainerGcmCePath -SettingsRoot $GcmSettingsRoot
    $arguments = ''
    $verb = 'open'
    $safe = $false
    $entry = $null
    if ($Mode -eq 'Manager') {
        $program = Resolve-TrainerManagerProgram $settings $ManagerPath $AppDirectory $GcmSettingsRoot
    } else {
        $safe = [bool](Get-TrainerProperty $settings 'safePath' $true)
        if ($Mode -eq 'Trainer') {
            $library = Get-GcmTrainerRoot $GcmSettingsRoot $GcmDefaultRoot
            $entry = Resolve-TrainerCurrentEntry -TrainerPath $TrainerPath -TrainerFolder $TrainerFolder -ExpectedSha256 $ExpectedSha256 -LibraryRoot $library -TrainerGameName $TrainerGameName -TrainerOrigin $TrainerOrigin -GcmResourceId $GcmResourceId
            $TrainerPath = $entry.path
        }
        $isTable = $Mode -eq 'Trainer' -and [IO.Path]::GetExtension($TrainerPath) -in @('.ct','.cetrainer')
        if ($Mode -eq 'CheatEngine' -or $isTable) {
            $ce = [string](Get-TrainerProperty $settings 'cePath' '')
            if (-not $ce) { throw '请先在修改器管理器中配置 Cheat Engine 路径' }
            $program = Resolve-TrainerCheatEngineProgram $ce
            if ([bool](Get-TrainerProperty $settings 'launchCTAsAdmin' $false)) { $verb = 'runas' }
            if ($isTable) { $arguments = '"' + $TrainerPath + '"' }
        } else {
            $program = $TrainerPath
            if ([bool](Get-TrainerProperty $settings 'launchAsAdmin' $true)) { $verb = 'runas' }
        }
    }
    return @{program=$program;arguments=$arguments;workingDirectory=[IO.Path]::GetDirectoryName($program);verb=$verb;safePath=$safe;mode=$Mode;trainer=$entry}
}
function Find-TrainerRunningProcess([string]$Program) {
    foreach ($process in [Diagnostics.Process]::GetProcessesByName([IO.Path]::GetFileNameWithoutExtension($Program))) {
        try { if ($process.MainModule.FileName -ieq $Program) { return $process } } catch { }
    }
    return $null
}
function Show-TrainerProcess($Process) {
    Initialize-TrainerLaunchNative
    $windows = [Collections.Generic.List[IntPtr]]::new()
    $targetPid = [uint32]$Process.Id
    $callback = [YmccTrainerLaunchNative+EnumProc]{param($window,$unused)
        $windowPid=[uint32]0
        [YmccTrainerLaunchNative]::GetWindowThreadProcessId($window,[ref]$windowPid)|Out-Null
        if($windowPid -eq $targetPid){
            $title=New-Object Text.StringBuilder 512
            [YmccTrainerLaunchNative]::GetWindowText($window,$title,512)|Out-Null
            if($title.Length -gt 0){$windows.Add($window)}
        }
        return $true
    }
    [YmccTrainerLaunchNative]::EnumWindows($callback,[IntPtr]::Zero)|Out-Null
    if (-not $windows.Count) { throw '程序已在运行，窗口尚未就绪，请稍后再试' }
    $window = $windows[0]
    foreach($candidate in $windows){if([YmccTrainerLaunchNative]::IsWindowVisible($candidate)){$window=$candidate;break}}
    [YmccTrainerLaunchNative]::ShowWindowAsync($window,9)|Out-Null
    if(-not [YmccTrainerLaunchNative]::SetForegroundWindow($window)){throw '程序已在运行，但系统未允许切换窗口，请从任务栏打开'}
}
function Start-TrainerLaunchPlan($Plan,[switch]$InteractiveShell) {
    # Reuse singleton manager/trainer processes; cheat tables still need their file argument.
    $running = if (-not $Plan.arguments) { Find-TrainerRunningProcess $Plan.program } else { $null }
    if(-not $running -and $InteractiveShell -and -not $Plan.arguments){$running=Find-TrainerCanonicalRunningProcess $Plan.program}
    if ($running) { Show-TrainerProcess $running; return @{reused=$true;pid=$running.Id} }
    if ($Plan.safePath) {
        $Plan.program = Get-TrainerSafeLaunchPath $Plan.program
        $Plan.workingDirectory = [IO.Path]::GetDirectoryName($Plan.program)
        if ($Plan.arguments) { $Plan.arguments = '"' + (Get-TrainerSafeLaunchPath $Plan.arguments.Trim('"')) + '"' }
    }
    if($InteractiveShell){
        $desktop=$null
        # Only failure BEFORE dispatch can keep the existing launcher.
        try{$desktop=Get-TrainerDesktopShell}catch{}
        if($desktop){try{return Invoke-TrainerShellPlan $Plan $desktop}finally{Close-TrainerDesktopShell $desktop}}
    }
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName=$Plan.program;$info.Arguments=$Plan.arguments;$info.WorkingDirectory=$Plan.workingDirectory
    $info.UseShellExecute=$true;$info.Verb=$Plan.verb
    try { $process=[Diagnostics.Process]::Start($info) }
    catch [ComponentModel.Win32Exception] {
        if ($_.Exception.NativeErrorCode -eq 1223) { throw '已取消系统权限确认，修改器未启动' }
        throw ('系统无法启动修改器（错误 '+$_.Exception.NativeErrorCode+'）：'+$_.Exception.Message)
    }
    if (-not $process) { throw '系统未返回启动进程，请从管理器确认启动状态' }
    return @{reused=$false;pid=$process.Id}
}
function Assert-TrainerLaunchTarget([string]$ExecutablePath,[int]$ProcessId,[string]$Created,[string]$ExpectedSha256) {
    if(-not $ProcessId -and -not $Created -and -not $ExpectedSha256){
        if($ExecutablePath -and -not (Find-TrainerRunningProcess $ExecutablePath)){throw '目标游戏已退出或更换，修改器已保留'}
        return
    }
    if($ProcessId -le 0 -or $Created -notmatch '^[1-9][0-9]*$' -or -not $ExecutablePath){throw '修改器启动缺少完整游戏进程身份'}
    Assert-TrainerSharedGameTarget $ExecutablePath $ProcessId $Created|Out-Null
    $observed=Get-TrainerProcessObservation $ProcessId $false
    if(-not $observed -or $observed.pid -ne $ProcessId -or $observed.created -cne $Created -or (Get-TrainerPathKey $observed.path) -ne (Get-TrainerPathKey $ExecutablePath)){throw '目标游戏进程已退出或更换，修改器已保留'}
    if($ExpectedSha256){
        if($ExpectedSha256 -notmatch '^[a-fA-F0-9]{64}$' -or (Get-FileHash -LiteralPath $ExecutablePath -Algorithm SHA256).Hash -ine $ExpectedSha256){throw '游戏 EXE 已改变，请重新识别当前游戏'}
        $after=Get-TrainerProcessObservation $ProcessId $false
        if(-not $after -or $after.pid -ne $ProcessId -or $after.created -cne $Created -or (Get-TrainerPathKey $after.path) -ne (Get-TrainerPathKey $ExecutablePath)){throw '核验期间目标游戏已退出或更换，修改器已保留'}
    }
    Assert-TrainerSharedGameTarget $ExecutablePath $ProcessId $Created|Out-Null
}
function Invoke-TrainerLaunch {
    param([string]$Mode, [string]$TrainerPath, [string]$TrainerFolder, [string]$ExpectedSha256, [string]$GameExecutablePath,
        [string]$CacheRoot,[string]$ResultPath,[string]$RequestId,
        [int]$GameProcessId=0,[string]$GameProcessCreated='',[string]$ExpectedGameSha256='',
        [string]$TrainerGameName='',[string]$TrainerOrigin='',[string]$GcmResourceId='',[string]$AppDirectory='',
        [string]$GcmSettingsRoot=(Join-Path $env:APPDATA 'GCM Settings'),[string]$GcmDefaultRoot=(Join-Path $env:APPDATA 'GCM Trainers'),
        [switch]$InteractiveShell)
    Assert-TrainerNotReparse $CacheRoot
    $ResultPath=Assert-TrainerChild $ResultPath (Join-Path $CacheRoot 'requests')
    if($RequestId -notmatch '^[a-zA-Z0-9-]{10,80}$' -or [IO.Path]::GetFileName($ResultPath) -ne ($RequestId+'.json')){throw '修改器启动任务身份无效'}
    $result=@{requestId=$RequestId;ok=$false;pending=$false}
    $plan=$null
    try {
        $plan=Resolve-TrainerLaunchPlan -Mode $Mode -TrainerPath $TrainerPath -TrainerFolder $TrainerFolder -ExpectedSha256 $ExpectedSha256 -TrainerGameName $TrainerGameName -TrainerOrigin $TrainerOrigin -GcmResourceId $GcmResourceId -AppDirectory $AppDirectory -GcmSettingsRoot $GcmSettingsRoot -GcmDefaultRoot $GcmDefaultRoot
        Assert-TrainerLaunchTarget $GameExecutablePath $GameProcessId $GameProcessCreated $ExpectedGameSha256
        $resolvedProgram=$plan.program
        $resolvedArguments=$plan.arguments
        $interactive=$Mode -eq 'Trainer' -and ($InteractiveShell -or (Test-TrainerSteamForeground $AppDirectory) -or (Test-TrainerBackgroundOwner $AppDirectory))
        $receipt=Start-TrainerLaunchPlan $plan -InteractiveShell:$interactive
        $result.ok=$true;$result.pid=$receipt.pid;$result.reused=$receipt.reused
        if($receipt.ContainsKey('launchBackend')){
            $result.launchBackend=$receipt.launchBackend;$result.window=$receipt.window;$result.focusRequested=$receipt.focusRequested;$result.notice=$receipt.notice;$result.launched=$true
            if($receipt.window -ne 'foreground'){
                # A real process is not proof of a usable foreground window.
                # Keep its PID and do not invite a duplicate execution.
                $result.ok=$false;$result.code='trainer-window-unconfirmed';$result.error=$receipt.notice
            }
        }
        $result.program=$resolvedProgram;$result.arguments=$resolvedArguments
        if($plan.trainer){$result.trainerPath=$plan.trainer.path;$result.trainerFolder=$plan.trainer.folder;$result.libraryRoot=$plan.trainer.libraryRoot}
    } catch {$result.error=$_.Exception.Message;$result.code=[string]$_.Exception.Data['trainerCode'];if($plan){$result.program=$plan.program;$result.arguments=$plan.arguments}}
    Write-TrainerJson $ResultPath $result
    if($Mode -in @('Trainer','CheatEngine','Manager')){
        # Keep one bounded diagnostic after the frontend consumes/removes the IPC receipt.
        $status=@{at=[DateTimeOffset]::Now.ToString('o');mode=$Mode;requestId=$RequestId;ok=$result.ok;gameExecutablePath=$GameExecutablePath;gameProcessId=$GameProcessId;gameProcessCreated=$GameProcessCreated;trainerPath=$TrainerPath;result=$result}
        try{Write-TrainerJson (Join-Path $CacheRoot 'last-launch-status.json') $status}catch{ # Diagnostic persistence cannot change the actual launch outcome.
        }
    }
}
