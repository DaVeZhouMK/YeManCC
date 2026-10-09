param([int]$Limit = 0)

function Resolve-RTSS {
    $candidates = @()
    try {
        $p = @(Get-Process -Name RTSS -ErrorAction SilentlyContinue | Select-Object -First 1)
        if ($p -and $p.Path) { $candidates += $p.Path }
    } catch { }
    $candidates += @(
        (Join-Path ${env:ProgramFiles(x86)} 'RivaTuner Statistics Server\RTSS.exe'),
        (Join-Path ${env:ProgramFiles} 'RivaTuner Statistics Server\RTSS.exe'),
        (Join-Path ${env:LOCALAPPDATA} 'RivaTuner Statistics Server\RTSS.exe')
    )
    foreach ($k in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*', 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*')) {
        Get-ItemProperty $k -ErrorAction SilentlyContinue | Where-Object { $_.DisplayName -match 'RivaTuner Statistics Server|RTSS' } | ForEach-Object {
            if ($_.InstallLocation) { $candidates += (Join-Path $_.InstallLocation 'RTSS.exe') }
            elseif ($_.DisplayIcon) { $candidates += ($_.DisplayIcon -replace ',.*$', '').Trim('"') }
        }
    }
    foreach ($candidate in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $dir = Split-Path -Parent (Resolve-Path -LiteralPath $candidate).Path
            if (Test-Path -LiteralPath (Join-Path $dir 'RTSSHooks64.dll') -PathType Leaf) {
                return @{ Exe=(Join-Path $dir 'RTSS.exe'); Dir=$dir }
            }
        }
    }
    throw 'RTSS.exe was not found; install RivaTuner Statistics Server'
}

$rtss = Resolve-RTSS
$GlobalPath = Join-Path $rtss.Dir 'Profiles\Global'
$DllPath    = Join-Path $rtss.Dir 'RTSSHooks64.dll'

# ensure RTSS is running so the profile reload takes effect
if (-not (Get-Process -Name "RTSS" -ErrorAction SilentlyContinue)) {
    try { Start-Process -FilePath $rtss.Exe -WorkingDirectory $rtss.Dir -WindowStyle Hidden; Start-Sleep -Milliseconds 800 } catch {}
}

# 1) write the new Limit into the Global profile file (regex match ^Limit=, same as working HTA)
try {
    $c = @(Get-Content -Path $GlobalPath -ErrorAction Stop)
    $hit = $false
    for ($i = 0; $i -lt $c.Count; $i++) {
        if ($c[$i] -match '^Limit=\d+') { $c[$i] = "Limit=$Limit"; $hit = $true; break }
    }
    if (-not $hit) { $c += "Limit=$Limit" }
    # 原子写：先写临时文件再 MoveFileEx/ReplaceFileW 替换，避免 RTSS 在游戏内持续读取时读到半截导致崩溃/损坏。
    # ⚠ 绝不做「截断式覆盖」：若目标被 RTSS 钩子短暂占用导致替换失败，重试若干次；仍失败则
    #   保留原 Global 不动（只留下临时文件供排查），绝不写半截 → 否则 RTSS 配置会被写坏、无法启动。
    $tmp = "$GlobalPath.tmp"
    Set-Content -Path $tmp -Value $c -Encoding Default
    Add-Type -MemberDefinition @'
        [DllImport("kernel32.dll",SetLastError=true)] public static extern bool MoveFileEx(string s,string d,int f);
        [DllImport("kernel32.dll",SetLastError=true)] public static extern bool ReplaceFile(string repl,string replwith,string backup,int dwReplaceFlags,System.IntPtr lpExclude,System.IntPtr lpClass);
'@ -Name RtssMv -Namespace YM -ErrorAction SilentlyContinue
    $replaced = $false
    for ($a = 0; $a -lt 6; $a++) {
        if ([YM.RtssMv]::MoveFileEx($tmp, $GlobalPath, 1)) { $replaced = $true; break }   # MOVEFILE_REPLACE_EXISTING=1
        Start-Sleep -Milliseconds 50
    }
    if (-not $replaced) {
        # ReplaceFileW 更能扛「目标正被其它进程打开」的场景（REPLACEFILE_WRITE_THROUGH=1）
        if ([YM.RtssMv]::ReplaceFile($GlobalPath, $tmp, $null, 1, [System.IntPtr]::Zero, [System.IntPtr]::Zero)) { $replaced = $true }
    }
    if (-not $replaced -and (Test-Path $tmp)) {
        # 替换失败：原 Global 保持完整，临时文件改名留存（不覆盖、不截断）
        try { Move-Item -Force $tmp "$GlobalPath.failed" -ErrorAction Stop } catch { }
    }
} catch {
    # file write failed (e.g. not elevated) - silently let RTSS continue
}

# 2) Call the RTSS SDK exports with their real signatures.
# LoadProfile is void(LPCSTR) and UpdateProfiles is void().  They are not
# rundll32 entry points; rundll32 would pass HWND/HINSTANCE/LPSTR/int and can
# make LoadProfile treat a window handle as a string pointer.
# ⚠ 不要 SaveProfile：那会让 RTSS 把"内存里的旧状态"写回磁盘，覆盖刚改的内容甚至写坏（损坏根因）。
try {
    if (-not [Environment]::Is64BitProcess) { throw '需要 64 位 PowerShell 才能调用 RTSSHooks64.dll' }
    $dllLiteral = $DllPath.Replace('\', '\\').Replace('"', '\"')
    $source = @"
using System;
using System.Runtime.InteropServices;
public static class YeManRtssProfileApi {
    [DllImport("$dllLiteral", EntryPoint="LoadProfile", CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Ansi)]
    public static extern void LoadProfile([MarshalAs(UnmanagedType.LPStr)] string profile);
    [DllImport("$dllLiteral", EntryPoint="UpdateProfiles", CallingConvention=CallingConvention.Cdecl)]
    public static extern void UpdateProfiles();
}
"@
    Add-Type -TypeDefinition $source -ErrorAction Stop
    [YeManRtssProfileApi]::LoadProfile('')
    [YeManRtssProfileApi]::UpdateProfiles()
} catch {
    Write-Error "RTSS profile reload failed: $($_.Exception.Message)"
}

if ($Limit -eq 0) { Write-Output "RTSS framerate limit DISABLED (no cap)" } else { Write-Output "RTSS framerate limit -> $Limit FPS" }
