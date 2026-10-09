# tdp_395_check.ps1
# 在 395 机器 (Strix Halo / Ryzen AI MAX+ 395) 上：右键 PowerShell -> 以管理员身份运行
# 然后执行：  & "本脚本完整路径\tdp_395_check.ps1"
# 把输出整段贴回来即可。
$ErrorActionPreference = 'SilentlyContinue'
$hr = '================================================'

Write-Host "$hr [1] 全盘找 RyzenSMU.bin（重点：有没有 38036 旧版） $hr"
$roots = @('C:\SOFT', ${env:ProgramFiles}, ${env:ProgramFiles(x86)}, 'C:\Program Files\Newko')
foreach ($r in $roots) {
  if (Test-Path $r) {
    Get-ChildItem -Path $r -Recurse -Filter RyzenSMU.bin -ErrorAction SilentlyContinue | ForEach-Object {
      $tag = if ($_.Length -eq 39652) { 'OK   39652 (UXTU/395 正确版)' }
             elseif ($_.Length -eq 38036) { 'BAD  38036 (Newko/无395 错版!)' }
             else { "size=$($_.Length)" }
      '{0,-66} {1}' -f $_.FullName, $tag
    }
  }
}

Write-Host "$hr [2] 找所有 YeManTdpCtl.exe（可能有多个安装互相干扰） $hr"
$exes = @()
foreach ($r in @('C:\SOFT', ${env:ProgramFiles}, ${env:ProgramFiles(x86)})) {
  if (Test-Path $r) { $exes += @(Get-ChildItem -Path $r -Recurse -Filter YeManTdpCtl.exe -ErrorAction SilentlyContinue) }
}
if ($exes.Count -eq 0) { Write-Host '!!! 没找到 YeManTdpCtl.exe，说明 TDP 执行体不在预期位置' }
else {
  foreach ($e in $exes) {
    $d = $e.DirectoryName
    $b1 = if (Test-Path (Join-Path $d 'RyzenSMU.bin')) { (Get-Item (Join-Path $d 'RyzenSMU.bin')).Length } else { 'NONE' }
    $b2 = if (Test-Path (Join-Path $d '_internal\RyzenSMU.bin')) { (Get-Item (Join-Path $d '_internal\RyzenSMU.bin')).Length } else { 'NONE' }
    Write-Host "EXE : $($e.FullName)"
    Write-Host "      bin@exe根目录 = $b1 | bin@_internal = $b2"
  }
}

Write-Host "$hr [3] PawnIO 驱动状态 $hr"
sc.exe query PawnIO | Out-String | Write-Host

Write-Host "$hr [4] info（实际加载哪个 bin / SMU 版本） $hr"
$exe = $exes | Select-Object -First 1
if ($exe) {
  & $exe.FullName info 2>&1 | Out-String | Write-Host

  Write-Host "$hr [5] set 50 实测 + 日志 $hr"
  & $exe.FullName set 50 --vendor amd 2>&1 | Out-String | Write-Host
  Start-Sleep -Seconds 1
  $log = Join-Path $env:TEMP 'yeman_pawnio_install.log'
  if (Test-Path $log) {
    Write-Host "--- 日志 $log (末尾 25 行) ---"
    Get-Content $log -Tail 25 | Write-Host
  } else { Write-Host "NO LOG @ $log" }
}
