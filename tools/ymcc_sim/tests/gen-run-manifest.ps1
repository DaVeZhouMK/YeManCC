# gen-run-manifest.ps1 - J0 RUN-MANIFEST 生成器（204 §6 A.1 / L36；Fan owner）
#   只读：按真实路径取身份（源码工作树 / native / 恢复服务 / Host / HC 及依赖 / payload / 前端 /
#   版本 / 两包 / 安装位 / Fan 工具树），全部给完整 SHA256 + 字节数，不只存缩写。
#   不猜目录：每个目标列出"实际命中路径"，缺失即如实记 exists=false，不虚构。
#   用法：powershell -File tools\ymcc_sim\tests\gen-run-manifest.ps1 [-OutPath <json>]
[CmdletBinding()]
param(
  [string]$OutPath = '',
  [int]$MaxTreeBytes = 64MB
)
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }
$testsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$simRoot = Split-Path -Parent $testsDir
$mainline = 'G:\YeManCC-Work\Mainline'
$srcRoot = Join-Path $mainline 'YeManCC-source'
$projRoot = Join-Path $srcRoot 'YeManCC'
$installRoot = 'C:\SOFT\YeMan'
if (-not $OutPath) { $OutPath = 'G:\YeManCC-Work\Docs\Tasks\Fan\FAN-920\RUN-MANIFEST.json' }

function Get-FileId([string]$Path, [string]$Label) {
  $exists = Test-Path -LiteralPath $Path -PathType Leaf
  if (-not $exists) { return [ordered]@{ label = $Label; path = $Path; exists = $false } }
  $f = Get-Item -LiteralPath $Path
  return [ordered]@{
    label = $Label; path = $Path; exists = $true; bytes = $f.Length
    sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    mtimeUtc = $f.LastWriteTimeUtc.ToString('o')
  }
}

function Get-TreeId([string]$Path, [string]$Label, [int]$MaxBytes) {
  $exists = Test-Path -LiteralPath $Path -PathType Container
  if (-not $exists) { return [ordered]@{ label = $Label; path = $Path; exists = $false } }
  $files = @(Get-ChildItem -LiteralPath $Path -Recurse -File -ErrorAction SilentlyContinue)
  $total = ($files | Measure-Object -Property Length -Sum).Sum
  if ($null -eq $total) { $total = 0 }
  $out = [ordered]@{ label = $Label; path = $Path; exists = $true; fileCount = @($files).Count; totalBytes = [long]$total }
  if ($total -gt $MaxBytes) {
    $out['fullTreeHash'] = $false
    $out['reason'] = ("tree {0:N0} bytes > guard {1:N0}; key files only (避免无界重扫)" -f $total, $MaxBytes)
    return $out
  }
  $lines = @()
  foreach ($f in ($files | Sort-Object FullName)) {
    $rel = $f.FullName.Substring($Path.Length).TrimStart('\')
    $lines += ($rel + '|' + (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash)
  }
  $joined = ($lines -join "`n")
  $sha = [Security.Cryptography.SHA256]::Create()
  $treeHash = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($joined))) -replace '-', ''
  $out['fullTreeHash'] = $true
  $out['treeSha256'] = $treeHash
  return $out
}

$manifest = [ordered]@{
  schema = 1
  name = 'J0-RUN-MANIFEST'
  generatedAt = (Get-Date).ToString('o')
  generatedBy = 'tools\ymcc_sim\tests\gen-run-manifest.ps1'
  authority = '204 §6 A.1 / L36（Fan 整理 J0 RUN-MANIFEST；完整 SHA256，按真实文件而非目录猜测）'
  scopeNote = 'Fan 范围（204-FAN 范围修订）；W3/W6/CPU 的对象只以引用其自身回执的形式出现，不代签'
  roots = [ordered]@{ mainline = $mainline; source = $srcRoot; project = $projRoot; install = $installRoot }
}

# ---- 1) 源码工作树身份 / 差集（Fan 关心 + W3 已交净差集的两处前端/原生） ----
$manifest['sourceWorktree'] = [ordered]@{
  files = @(
    Get-FileId (Join-Path $projRoot 'native\main.cpp') 'native/main.cpp (W6/W3 区; Fan 只读引用)'
    Get-FileId (Join-Path $projRoot 'native\dsu_server.cpp') 'native/dsu_server.cpp (只读，未改)'
    Get-FileId (Join-Path $projRoot 'FanLab\real-host\Program.cs') 'FanLab/real-host/Program.cs (Fan 域)'
    Get-FileId (Join-Path $projRoot 'src\bridge\fanHost.ts') 'src/bridge/fanHost.ts (W3 独占)'
    Get-FileId (Join-Path $projRoot 'src\views\FanView.vue') 'src/views/FanView.vue (W3 独占)'
    Get-FileId (Join-Path $projRoot 'InputHost\Program.cs') 'InputHost/Program.cs (W6/W3 区)'
  )
  baselineAsOf204Plan = [ordered]@{
    'native/main.cpp' = '51CA1AB8DD994B5F607188E412C48723EBC166073A1B3AB01D53DC1E08A7FEF4'
    'src/bridge/fanHost.ts' = '387650C912488B9A…(204 基线, 见 W3 净差集包)'
    'src/views/FanView.vue' = 'E5E9619E85F54402…(204 基线, 见 W3 净差集包)'
    note = '上述基线来自 204 §1/§2；当前值见 files。真实漂移（如 main.cpp → 796C145E）由 W3 净差集包交接，Fan 不代签'
  }
  fanOwnProductWrites = [ordered]@{
    thisBatch = @()   # 204-M/N：Fan 零产品写入（只动 tools/ymcc_sim、Docs、板面）
    historical = @(
      'src/bridge/fanHost.ts 的最小补丁由 Fan 提供、W3 owner 写入并跑门（203 决策 5）',
      'Build/Release/version/packages 由 Fan 作为唯一整合/制包 owner 串行产出（见下方 native/host/payload/frontend/packages 段）'
    )
  }
}

# ---- 2) native / 恢复服务 / Host（构建、Release、安装三处对照） ----
$manifest['native'] = @(
  Get-FileId (Join-Path $mainline 'Build\App\Native\YeManCC.exe') 'build native'
  Get-FileId (Join-Path $mainline 'Release\YeManCC\YeManCC.exe') 'release native'
  Get-FileId (Join-Path $installRoot 'YeManCC\YeManCC.exe') 'install native'
  Get-FileId (Join-Path $mainline 'Build\App\Native\YeManRecoveryService.exe') 'build recovery'
  Get-FileId (Join-Path $mainline 'Release\YeManCC\YeManRecoveryService.exe') 'release recovery'
  Get-FileId (Join-Path $installRoot 'YeManCC\YeManRecoveryService.exe') 'install recovery'
)
$manifest['host'] = @(
  Get-FileId (Join-Path $mainline 'Release\PowerControl\fan-host\YeManFanHost.json') 'release host manifest'
  Get-FileId (Join-Path $mainline 'Release\PowerControl\fan-host\YeManFanHost.payload.json') 'release payload manifest'
  Get-FileId (Join-Path $mainline 'Release\PowerControl\fan-host\YeManFanHost.exe') 'release host exe'
  Get-FileId (Join-Path $mainline 'Build\Package\CompleteRoot\PowerControl\fan-host\YeManFanHost.exe') 'package-root host exe'
  Get-FileId (Join-Path $installRoot 'PowerControl\fan-host\YeManFanHost.exe') 'install host exe'
  Get-FileId (Join-Path $mainline 'Release\PowerControl\fan-host\YeManFanHost.dll') 'release host business dll (D-204-9 关注点)'
  Get-FileId (Join-Path $installRoot 'PowerControl\fan-host\YeManFanHost.dll') 'install host business dll (D-204-9 关注点)'
)

# ---- 3) HC 及依赖 ----
$hcPattern = Join-Path $installRoot 'PowerControl\handheldcompanion-runtime\*\HandheldCompanion.dll'
$hcInstall = @(Get-ChildItem -Path $hcPattern -ErrorAction SilentlyContinue | Select-Object -First 1)
$hcRel = @(Get-ChildItem -Path (Join-Path $mainline 'Release\PowerControl\handheldcompanion-runtime\*\HandheldCompanion.dll') -ErrorAction SilentlyContinue | Select-Object -First 1)
$manifest['hc'] = [ordered]@{
  installDll = $(if ($hcInstall) { Get-FileId $hcInstall[0].FullName 'install HC dll' } else { [ordered]@{ label = 'install HC dll'; exists = $false } })
  releaseDll = $(if ($hcRel) { Get-FileId $hcRel[0].FullName 'release HC dll' } else { [ordered]@{ label = 'release HC dll'; exists = $false } })
  runtimeTree = Get-TreeId (Join-Path $installRoot 'PowerControl\handheldcompanion-runtime') 'install HC runtime tree' $MaxTreeBytes
}

# ---- 4) payload / 前端 / 版本 / 两包 / 安装位 ----
$manifest['payload'] = [ordered]@{
  releaseFanHostTree = Get-TreeId (Join-Path $mainline 'Release\PowerControl\fan-host') 'release fan-host payload dir' $MaxTreeBytes
  releaseLightSetter = Get-FileId (Join-Path $mainline 'Release\PowerControl\light-setter\YeManLightSetter.exe') 'release light-setter exe'
  installLightSetter = Get-FileId (Join-Path $installRoot 'PowerControl\light-setter\YeManLightSetter.exe') 'install light-setter exe'
}
$manifest['frontend'] = Get-TreeId (Join-Path $mainline 'Build\App\Web') 'build frontend (Web)' $MaxTreeBytes
$manifest['version'] = @(
  Get-FileId (Join-Path $mainline 'Release\version.json') 'release version.json'
  Get-FileId (Join-Path $installRoot 'YeManCC\version.json') 'install version.json'
)
$verText = $null
$vj = Join-Path $mainline 'Release\version.json'
if (Test-Path -LiteralPath $vj) { try { $verText = (Get-Content -LiteralPath $vj -Raw -Encoding UTF8 | ConvertFrom-Json).version } catch { $verText = 'unparseable' } }
$manifest['versionValue'] = [string]$verText
$manifest['packages'] = @(
  Get-FileId (Join-Path $mainline 'Release\Packages\YeManCC.zip') 'complete package (YeManCC.zip; the only package since 2026-09-23)'
)
$manifest['install'] = [ordered]@{
  appExe = Get-FileId (Join-Path $installRoot 'YeManCC\YeManCC.exe') 'install app exe'
  fanHostExe = Get-FileId (Join-Path $installRoot 'PowerControl\fan-host\YeManFanHost.exe') 'install fan host exe'
  featureAssets = Get-TreeId (Join-Path $installRoot 'PowerControl\feature-assets') 'install feature-assets tree' $MaxTreeBytes
}

# ---- 5) Fan 工具树（tools/ymcc_sim；本线唯一维护者） ----
$manifest['fanTools'] = Get-TreeId $simRoot 'repo tools/ymcc_sim tree' (512MB)

# ---- 6) 交叉引用（他线材料只引用，不代签） ----
$manifest['crossLineReferences'] = [ordered]@{
  w3NetDiff = 'Mainline/Temp/coordination/inbox/Fan/… 与 W3 报告 §1.1（W3-NETDIFF-IDENTITY.json BE80A66D…）；本清单不重复其逐字节证明'
  w6Entry = 'R203-W6 = DONE-0.0.33-REBIND-RERUN-ALL-GREEN（板面）'
  cpu = 'CPU 归属见 204-FAN 范围修订 §2.2：不属 Fan；本清单不载 CPU 结论'
}

[IO.File]::WriteAllText($OutPath, ($manifest | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
Write-Host ("RUN-MANIFEST 已写出：{0}" -f $OutPath) -ForegroundColor Green
$bytes = [IO.File]::ReadAllBytes($OutPath)
Write-Host ("bytes={0} sha256={1}" -f $bytes.Length, (([BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($bytes))) -replace '-', ''))
exit 0