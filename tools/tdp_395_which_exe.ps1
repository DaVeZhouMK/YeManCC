# tdp_395_which_exe.ps1
# 在 395 机器上：右键 PowerShell -> 以管理员身份运行，然后执行：
#   & "本脚本完整路径\tdp_395_which_exe.ps1"
# 把整段输出贴回即可。
$ErrorActionPreference = 'SilentlyContinue'
$hr = '================================================'

# [A] 全盘关键目录找所有 YeManTdpCtl.exe
$roots = @('C:\SOFT', ${env:ProgramFiles}, ${env:ProgramFiles(x86)})
$exes = @()
foreach ($r in $roots) {
  if (Test-Path $r) {
    $exes += @(Get-ChildItem -Path $r -Recurse -Filter YeManTdpCtl.exe -ErrorAction SilentlyContinue)
  }
}
Write-Host "$hr [A] 找到的 exe (数量=$($exes.Count)) $hr"
if ($exes.Count -eq 0) { Write-Host '!!! 一个都没找到' }
foreach ($e in $exes) { '{0}  size={1}' -f $e.FullName, $e.Length | Write-Host }

# [B] 逐个 exe 跑 info：旧版会显示 C:\SOFT\PowerControl\pawnio\ (无 YeMan)，新版显示 C:\SOFT\YeMan\PowerControl\pawnio\
Write-Host "$hr [B] 逐个 exe 的 info（看加载哪个 bin 路径，判断旧/新） $hr"
foreach ($e in $exes) {
  Write-Host ">>> $($e.FullName)"
  & $e.FullName info 2>&1 | Out-String | Write-Host
}

# [C] 直接测 APP 默认调用路径：C:\SOFT\YeMan\PowerControl\pawnio\YeManTdpCtl.exe
#     看到 (cmd=20) 即新版OK；看到 (0x4F) 即旧版FAIL
Write-Host "$hr [C] 直接测 APP 默认路径 exe 能否写 395 (cmd=20=新版) $hr"
$appExe = 'C:\SOFT\YeMan\PowerControl\pawnio\YeManTdpCtl.exe'
if (Test-Path $appExe) {
  & $appExe set 50 --vendor amd 2>&1 | Out-String | Write-Host
  Write-Host '--- 上面若看到 (cmd=20) 即新版OK；看到 (0x4F) 即旧版FAIL ---'
} else {
  Write-Host "APP 默认 exe 不存在: $appExe  (说明 APP 实际调的是别处的 exe)"
}
