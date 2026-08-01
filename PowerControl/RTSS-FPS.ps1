param([int]$Limit = 0)

$GlobalPath = "C:\Program Files (x86)\RivaTuner Statistics Server\Profiles\Global"
$DllPath    = "C:\Program Files (x86)\RivaTuner Statistics Server\RTSSHooks64.dll"
$Rundll     = "C:\Windows\System32\rundll32.exe"

# ensure RTSS is running so the profile reload takes effect
$rtssExe = "C:\Program Files (x86)\RivaTuner Statistics Server\RTSS.exe"
if (-not (Get-Process -Name "RTSS" -ErrorAction SilentlyContinue)) {
    try { Start-Process -FilePath $rtssExe -WindowStyle Hidden; Start-Sleep -Milliseconds 800 } catch {}
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

# 2) rundll32 calls into RTSSHooks64.dll: LoadProfile / UpdateProfiles
# ⚠ 不要 SaveProfile：那会让 RTSS 把"内存里的旧状态"写回磁盘，覆盖刚改的内容甚至写坏（损坏根因）。
foreach ($entry in @("LoadProfile","UpdateProfiles")) {
    $dllArg = '"{0}",{1}' -f $DllPath, $entry
    try {
        Start-Process -FilePath $Rundll -ArgumentList $dllArg -WindowStyle Hidden -Wait -ErrorAction Stop
    } catch {}
}

if ($Limit -eq 0) { Write-Output "RTSS framerate limit DISABLED (no cap)" } else { Write-Output "RTSS framerate limit -> $Limit FPS" }
