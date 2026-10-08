<#
.SYNOPSIS
  Build YeManCC into the workspace Build area without touching the installed product.

.DESCRIPTION
  Local workspace layout:
    YeManCC-source\YeManCC -> Build\App\Web
                            -> Build\App\Native

  CI can set YEMAN_WORKSPACE_ROOT to keep Build and Release inside the checkout.
#>
[CmdletBinding()]
param(
  [string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT
)

$ErrorActionPreference = 'Stop'

# ---- module-path self-heal ----------------------------------------------------------
# Same measured remedy as tools/ymcc_sim/lib/sim_core.ps1 (2026-09-22) and
# tools/opt-inheritance-gate.ps1: when this Windows PowerShell 5.1 process is launched
# through node/pnpm (as `pnpm run build` does, and as CI does under shell: pwsh) it
# inherits PowerShell 7's PSModulePath. The leading PowerShell 7 entries shadow
# Microsoft.PowerShell.Utility, so autoloading of Get-FileHash / Get-Content fails with
# "CommandNotFoundException" here or in any script this one invokes with the call
# operator. Put this host's own module dir first. Idempotent.
$buildHomeModules = Join-Path $PSHOME 'Modules'
$buildFirstEntry = ($env:PSModulePath -split ';')[0]
if (-not $buildFirstEntry -or ($buildFirstEntry.TrimEnd('\') -ine $buildHomeModules.TrimEnd('\'))) {
  $env:PSModulePath = $buildHomeModules + ';' + $env:PSModulePath
}

function Get-FullPath([string]$Path) {
  return [IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Assert-ChildPath([string]$Child, [string]$Parent, [string]$Label) {
  $childFull = Get-FullPath $Child
  $parentFull = Get-FullPath $Parent
  if (-not $childFull.StartsWith($parentFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "$Label is outside the workspace: $childFull"
  }
}

function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = [IO.File]::OpenRead($Path)
    try {
      return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '')
    } finally {
      $stream.Dispose()
    }
  } finally {
    $sha.Dispose()
  }
}

$ProjectRoot = Get-FullPath (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
  $WorkspaceRoot = Get-FullPath (Join-Path $ProjectRoot '..\..')
} else {
  $WorkspaceRoot = Get-FullPath $WorkspaceRoot
}

$BuildRoot = Join-Path $WorkspaceRoot 'Build'
$AppBuildRoot = Join-Path $BuildRoot 'App'
$WebBuild = Join-Path $AppBuildRoot 'Web'
$NativeBuild = Join-Path $AppBuildRoot 'Native'

Assert-ChildPath $AppBuildRoot $WorkspaceRoot 'Build output'
if ((Get-FullPath $AppBuildRoot) -ne (Get-FullPath (Join-Path $WorkspaceRoot 'Build\App'))) {
  throw "Unexpected Build output: $AppBuildRoot"
}

# ================================================================
# FanLab source baseline follows the batch (2026-09-27).
# `pnpm run test:fanhost-source-baseline` is a read-only, fail-closed check that lives OUTSIDE this
# chain; an approved Fan batch that touched FanLab/real-host sources used to leave it red until someone
# re-recorded the baseline by hand (the 2026-09-02 snapshot went stale exactly that way). The build
# (and package-release.ps1) now keep it in step: read-only preview, recompute from the tree, print and
# record the delta, keep previous values nested, then re-run the read-only verifier as the cross-check.
# No-op when already in step; any failure aborts the build.
# ================================================================
$ensureSourceBaseline = Join-Path $ProjectRoot 'tools\ensure-fanhost-source-baseline.ps1'
if (!(Test-Path -LiteralPath $ensureSourceBaseline -PathType Leaf)) {
  throw "FanLab source-baseline guard missing: $ensureSourceBaseline"
}
& $ensureSourceBaseline

# ================================================================
# 发布前门（R-publisher，2026-09-11 + 2026-09-12 R-publisher-final）：
# 任一项失败即 halt。
# 1) install-fan-host-payload.ps1：Parser::ParseFile == 0 错 + UTF-8 BOM 断言
# 2) 哈希链自动重算对拍：ps1 → payload files[] → payload 自身 ←→ main.cpp
#    $expectedFanHostV2ManifestSha256 逐字对拍；不再人工记哈希。
# 3) 标记契约自动对拍：ps1 成功输出常量（FAN_HOST_PAYLOAD_OK）与
#    fanHost.ts 成功判定正则双向兼容，不一致即 halt。
# 4) 晋级门（920 §8.0-C，2026-09-20）：冻结 payload 必须携带本批审核通过的
#    Host 业务 DLL。apphost EXE 相同不能证明 DLL 相同；主线构建有意跳过 Host
#    构建，因此若 Host 构建产物存在则逐字对拍，缺失则明确报告不可验证
#    （unverifiable），不安静 PASS。
# ================================================================
function Assert-PublishPreGate([string]$ProjectRoot) {
  $fanHostDir = Join-Path $ProjectRoot 'PowerControl\fan-host'
  $installPs1 = Join-Path $fanHostDir 'install-fan-host-payload.ps1'
  $manifestPath = Join-Path $fanHostDir 'YeManFanHost.payload.json'
  $nativeMain = Join-Path $ProjectRoot 'native\main.cpp'
  if (-not (Test-Path -LiteralPath $installPs1 -PathType Leaf)) { throw "前门: 缺少 install-fan-host-payload.ps1: $installPs1" }
  if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "前门: 缺少 YeManFanHost.payload.json: $manifestPath" }
  if (-not (Test-Path -LiteralPath $nativeMain -PathType Leaf)) { throw "前门: 缺少 native\main.cpp: $nativeMain" }

  # 1a) PS 解析 0 错（PS 5.1 兼容：ParseFile 不执行脚本）
  $tokens = $null; $parseErrors = $null
  [System.Management.Automation.Language.Parser]::ParseFile($installPs1, [ref]$tokens, [ref]$parseErrors) | Out-Null
  if ($parseErrors.Count -ne 0) {
    throw ("前门: install-fan-host-payload.ps1 Parser 错误(" + $parseErrors.Count + "): " + ($parseErrors[0].Message))
  }
  # 1b) UTF-8 BOM 断言（R3-redo 起脚本必须 BOM；丢失 BOM 会让 PS5.1 误解码中文注释吞掉下一条语句）
  $firstBytes = [IO.File]::ReadAllBytes($installPs1)
  if ($firstBytes.Length -lt 3 -or $firstBytes[0] -ne 239 -or $firstBytes[1] -ne 187 -or $firstBytes[2] -ne 191) {
    throw '前门: install-fan-host-payload.ps1 必须为 UTF-8 BOM（禁止无 BOM 非 ASCII）'
  }

  # 2) 哈希链自动重算对拍
  $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
  if ([int]$manifest.schemaVersion -ne 2) { throw '前门: fan-host payload manifest schemaVersion != 2' }
  # 2a) ps1 实算哈希 == payload files[] 声明
  $ps1Actual = (Get-Sha256 $installPs1).ToLowerInvariant()
  $ps1Declared = @($manifest.files | Where-Object { ([string]$_.path) -eq 'install-fan-host-payload.ps1' })
  if ($ps1Declared.Count -ne 1) { throw '前门: payload manifest 未声明 install-fan-host-payload.ps1' }
  if ($ps1Actual -ne ([string]$ps1Declared[0].sha256).ToLowerInvariant()) {
    throw ("前门: ps1 哈希链不一致：实算=" + $ps1Actual + " 声明=" + ([string]$ps1Declared[0].sha256).ToLowerInvariant() + "（修改 ps1 必须重算 payload files[]，禁止手填）")
  }
  # 2b) payload 自身文件级 SHA256 == main.cpp expectedFanHostV2ManifestSha256
  $manifestActual = (Get-Sha256 $manifestPath).ToLowerInvariant()
  $mainCpp = Get-Content -LiteralPath $nativeMain -Raw -Encoding UTF8
  $match = [regex]::Match($mainCpp, '\$expectedFanHostV2ManifestSha256\s*=\s*''([0-9A-Fa-f]{64})''')
  if (-not $match.Success) { throw '前门: main.cpp 缺少 $expectedFanHostV2ManifestSha256 常量' }
  $mainDeclared = $match.Groups[1].Value.ToLowerInvariant()
  if ($manifestActual -ne $mainDeclared) {
    throw ("前门: manifest 哈希链不一致：实算=" + $manifestActual + " main.cpp声明=" + $mainDeclared + "（payload 内容变化必须实算重填 main.cpp，禁止手改）")
  }
  # 2c) 晋级门（920 §8.0-C）：冻结 payload 必须携带本批审核通过的 Host 业务 DLL，
  #     不能默默携带旧 Host。apphost EXE 哈希相同不能证明 Host 相同（业务逻辑在
  #     YeManFanHost.dll）；主线构建有意跳过 Host 构建（FAN_HOST_BUILD=SKIPPED），
  #     所以只在 Host 构建产物存在时逐字对拍，缺失时明确报告不可验证而非安静 PASS。
  $payloadDll = Join-Path $fanHostDir 'YeManFanHost.dll'
  $payloadDllEntry = @($manifest.files | Where-Object { ([string]$_.path) -eq 'YeManFanHost.dll' })
  if ($payloadDllEntry.Count -ne 1) { throw '前门: payload manifest 未声明 YeManFanHost.dll' }
  $payloadDllDeclared = ([string]$payloadDllEntry[0].sha256).ToLowerInvariant()
  if (-not (Test-Path -LiteralPath $payloadDll -PathType Leaf)) { throw "前门: payload 缺少 YeManFanHost.dll: $payloadDll" }
  $payloadDllActual = (Get-Sha256 $payloadDll).ToLowerInvariant()
  if ($payloadDllActual -ne $payloadDllDeclared) {
    throw ("前门: payload Host DLL 与 manifest 声明不一致：实算=" + $payloadDllActual + " 声明=" + $payloadDllDeclared)
  }
  $hostBuildDll = Join-Path $ProjectRoot 'FanLab\real-host\bin\Release\net10.0-windows10.0.19041.0\win-x64\YeManFanHost.dll'
  if (Test-Path -LiteralPath $hostBuildDll -PathType Leaf) {
    $hostBuildDllHash = (Get-Sha256 $hostBuildDll).ToLowerInvariant()
    if ($hostBuildDllHash -ne $payloadDllActual) {
      # 2026-09-23 operator 裁决：**现在都是主线导出** —— 冻结 payload（= 主线导出实物）与本机
      # Host 构建产物不一致时**不再阻断构建**（此前 throw 会把整条手柄线拦住）。
      # 事实照记不猜：两个原值写进门报告并显式告警；出货前的实际校验仍在制包侧 ——
      # package-release.ps1 的 Assert-FanHostMainlinePayload 对主线导出逐件重钉，
      # update-manifest 声明 rules.fanHost=replace（更新会带走主线 Host）。
      Write-Warning ("前门(已降级为警告): 冻结 payload 与本机 Host 构建产物不同 —— 主线导出制下不阻断；" +
        "payload DLL = " + $payloadDllActual + " ; Host build DLL = " + $hostBuildDllHash)
      $hostDllGate = "warning-mismatch(payload=" + $payloadDllActual + ",hostBuild=" + $hostBuildDllHash + ")"
    } else {
      $hostDllGate = "verified(" + $hostBuildDllHash + ")"
    }
  } else {
    $hostDllGate = 'unverifiable(no-host-build-output)'
    Write-Warning ("前门: 未找到 Host 构建产物，payload Host DLL 身份不可验证：" + $hostBuildDll)
  }
  # 3) 标记契约自动对拍（R-publisher-final，2026-09-12）：ps1 的成功输出常量
  #    （Write-Output "FAN_HOST_*_OK: ..."）必须被 fanHost.ts 成功判定正则接受，
  #    且正则必须覆盖 ps1 当前产出的每一个常量（双向兼容：ps1 改动没同步 TS
  #    → halt）。历史分支（如 ACL 版已不再产出）允许保留在正则中以兼容旧安装，
  #    但不要求 ps1 必须同时产出全部历史分支。任何一侧改动而另一侧未同步 →
  #    halt 并打印两侧字面量。
  $fanHostTs = Join-Path $ProjectRoot 'src\bridge\fanHost.ts'
  if (-not (Test-Path -LiteralPath $fanHostTs -PathType Leaf)) { throw "前门: 缺少 fanHost.ts: $fanHostTs" }
  $ps1Text = Get-Content -LiteralPath $installPs1 -Raw -Encoding UTF8
  $tsText = Get-Content -LiteralPath $fanHostTs -Raw -Encoding UTF8
  # 3a) ps1 侧成功输出常量：/FAN_HOST_[A-Z_]+_OK:/
  $ps1Markers = @([regex]::Matches($ps1Text, 'FAN_HOST_[A-Z_]+_OK:') | ForEach-Object { $_.Value } | Sort-Object -Unique)
  if ($ps1Markers.Count -eq 0) { throw '前门: install-fan-host-payload.ps1 没有 FAN_HOST_*_OK 成功输出常量' }
  # 3b) fanHost.ts 侧成功判定正则：提取 "FAN_HOST_(ACL|PAYLOAD)_OK:" 字面量
  $tsRegexMatch = [regex]::Match($tsText, 'FAN_HOST_[^/]+_OK:')
  if (-not $tsRegexMatch.Success) { throw '前门: fanHost.ts 缺少 FAN_HOST_*_OK 成功判定正则' }
  $tsMarkerPattern = $tsRegexMatch.Value   # 如 "FAN_HOST_(ACL|PAYLOAD)_OK:"
  # 3c) 双向断言：
  #    方向一：每个 ps1 输出常量必须被 TS 正则接受（把 TS 正则作为匹配模式）
  $tsRegex = [regex]::new($tsMarkerPattern, [Text.RegularExpressions.RegexOptions]::IgnoreCase)
  $ps1NotAccepted = @($ps1Markers | Where-Object { -not $tsRegex.IsMatch($_) })
  #    方向二：TS 正则必须覆盖 ps1 当前产出的每个常量（TS 正则应含与之同名的分支）
  $ps1Branches = @($ps1Markers | ForEach-Object { [regex]::Match($_, 'FAN_HOST_([A-Z_]+)_OK').Groups[1].Value })
  $missingBranches = @($ps1Branches | Where-Object { $tsMarkerPattern -notmatch ('FAN_HOST_\(' + [regex]::Escape($_) + '\|') -and $tsMarkerPattern -notmatch ('\|' + [regex]::Escape($_) + '\)') -and $tsMarkerPattern -notmatch ([regex]::Escape($_)) })
  if ($ps1NotAccepted.Count -gt 0 -or $missingBranches.Count -gt 0) {
    throw ("前门: 标记契约不一致`n  ps1 常量: " + ($ps1Markers -join ', ') +
      "`n  TS 正则: " + $tsMarkerPattern +
      "`n  ps1 不被接受: " + $(if($ps1NotAccepted){$ps1NotAccepted -join ', '}else{'(无)'}) +
      "`n  TS 未覆盖 ps1 分支: " + $(if($missingBranches){$missingBranches -join ', '}else{'(无)'}))
  }
  Write-Output ("PRE_GATE_OK  ps1=" + $ps1Actual + " manifest=" + $manifestActual +
    " hostDll=" + $hostDllGate +
    " marker(" + ($ps1Markers -join ',') + ")<->ts(" + $tsMarkerPattern + ")")
}
Assert-PublishPreGate $ProjectRoot
& (Join-Path $PSScriptRoot 'verify-input-host-dependencies.ps1') -ProjectRoot $ProjectRoot
& (Join-Path $PSScriptRoot 'verify-source-release-inputs.ps1') -ProjectRoot $ProjectRoot
& (Join-Path $PSScriptRoot 'verify-hc-source-baseline.ps1') -RuntimeRoot (Join-Path $ProjectRoot 'deps\handheldcompanion-runtime')


if (Test-Path -LiteralPath $AppBuildRoot) {
  Remove-Item -LiteralPath $AppBuildRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $WebBuild, $NativeBuild | Out-Null

$env:YEMAN_WORKSPACE_ROOT = $WorkspaceRoot
$env:YEMAN_BUILD_WEB_DIR = $WebBuild

# Fan Host is optional and excluded from the normal mainline build. It is
# managed separately so an unfinished payload cannot enter the application.
if ($env:YEMAN_BUILD_FAN_HOST -eq '1') {
  throw 'Fan Host rebuild is disabled by the mainline release policy.'
}
Write-Output 'FAN_HOST_BUILD=SKIPPED (release uses the source fan-host payload)'

Push-Location $ProjectRoot
try {
  & node (Join-Path $ProjectRoot 'scripts\write-version.mjs')
  if ($LASTEXITCODE -ne 0) { throw "Version generation failed: exit=$LASTEXITCODE" }

  & pnpm exec vue-tsc --noEmit
  if ($LASTEXITCODE -ne 0) { throw "Type check failed: exit=$LASTEXITCODE" }

  & pnpm exec vite build
  if ($LASTEXITCODE -ne 0) { throw "Frontend build failed: exit=$LASTEXITCODE" }

  Copy-Item -LiteralPath (Join-Path $ProjectRoot 'app.config.json') -Destination (Join-Path $WebBuild 'app.config.json') -Force

  & cmd.exe /d /c (Join-Path $ProjectRoot 'native\build_native.bat')
  if ($LASTEXITCODE -ne 0) { throw "Native build failed: exit=$LASTEXITCODE" }

  $inputHostProj = Join-Path $ProjectRoot 'InputHost\YeManInputHost.csproj'
  if (Test-Path -LiteralPath $inputHostProj) {
    & dotnet build $inputHostProj -c Release --nologo
    if ($LASTEXITCODE -ne 0) { throw "InputHost build failed: exit=$LASTEXITCODE" }
    $hostOut = Join-Path $ProjectRoot 'InputHost\bin\Release\net10.0-windows'
    $hostDest = Join-Path $NativeBuild 'InputHost'
    New-Item -ItemType Directory -Force -Path $hostDest | Out-Null
    Copy-Item -Path (Join-Path $hostOut '*') -Destination $hostDest -Force
    # 2026-09-16 用户裁决「不需要就去掉」：原先这里会把锁定版 HIDMaestro 运行时（外部
    # Migration-Backup 里的 HIDMaestroTest 目录）的 WinRT 投影复制进 InputHost 导出 lane，
    # 并对其做存在性硬校验。已核实两条都不需要：
    #   ① 元数据：HIDMaestro.Core.dll 的 AssemblyRef 全表只有 BCL，无 WinRT/SDK 投影、
    #      字符串扫描 0 命中（排除反射式用法）；
    #   ② 运行时：InputHost 自身输出、导出树、装位 virtual-gamepad 历来都不带投影，
    #      而产品每次运行都能正常创建虚拟手柄（deploy 的 strip 注释亦为长期既有）。
    # ⇒ 复制与校验整块移除，构建不再依赖那个外部备份路径。
  }

  # light-setter（L1 方案 B，2026-09-16）：一次性灯光设置器（直驱 HC 程序集，
  # 覆盖非 ROG 机型的灯光后端）。publish 输出为"源 payload"（落 PowerControl\
  # light-setter，与 fan-host 同模式），由 deploy-installed.ps1 复制到安装位。
  $lightSetterProj = Join-Path $ProjectRoot 'FanLab\LightSetter\YeManLightSetter.csproj'
  if (Test-Path -LiteralPath $lightSetterProj) {
    & dotnet publish $lightSetterProj -c Release --nologo
    if ($LASTEXITCODE -ne 0) { throw "LightSetter publish failed: exit=$LASTEXITCODE" }
    $lightSetterPublish = Join-Path $ProjectRoot 'FanLab\LightSetter\bin\Release\net10.0-windows\win-x64\publish'
    if (-not (Test-Path -LiteralPath $lightSetterPublish -PathType Container)) {
      throw "LightSetter publish output not found: $lightSetterPublish"
    }
    $lightSetterDest = Join-Path $ProjectRoot 'PowerControl\light-setter'
    New-Item -ItemType Directory -Force -Path $lightSetterDest | Out-Null
    Copy-Item -Path (Join-Path $lightSetterPublish '*') -Destination $lightSetterDest -Force
    # 2026-09-16 用户裁决：TFM 去掉 -windows10.0.19041.0 后不再随包 WinRT 投影；
    # 清掉历史遗留副本（copy 不删除，旧文件会一直留在源 payload 与安装位）。
    foreach ($stale in @('Microsoft.Windows.SDK.NET.dll', 'WinRT.Runtime.dll')) {
      $stalePath = Join-Path $lightSetterDest $stale
      if (Test-Path -LiteralPath $stalePath) { Remove-Item -LiteralPath $stalePath -Force }
    }
  }
} finally {
  Pop-Location
}

$required = @(
  (Join-Path $WebBuild 'index.html'),
  (Join-Path $WebBuild 'assets'),
  (Join-Path $WebBuild 'app.config.json'),
  (Join-Path $NativeBuild 'YeManCC.exe'),
  (Join-Path $NativeBuild 'YeManRecoveryService.exe'),
  (Join-Path $NativeBuild 'YMCCRtssProfileHelper.exe'),
  (Join-Path $NativeBuild 'YMCCOverlayBridge.dll'),
  (Join-Path $NativeBuild 'InputHost\YeManInputHost.exe'),
  (Join-Path $NativeBuild 'InputHost\HIDMaestro.Core.dll'),
  (Join-Path $NativeBuild 'InputHost\Nefarius.Utilities.DeviceManagement.dll')
)
foreach ($path in $required) {
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Build output is incomplete: $path"
  }
}

# ================================================================
# Adopted-optimization inheritance gate (release ruling, 2026-09-20).
# Captures THIS build's source-tree digest + toolchain + dependency + product
# hashes and requires every adopted optimization to be present (binary marker +
# source anchor). Fail-closed: a missing optimization or an unverifiable build
# aborts BEFORE BUILD_OK, so no packaging / deployment can start from a build
# that does not inherit the adopted optimization set.
# ================================================================
$OptGateScript = Join-Path $ProjectRoot 'tools\opt-inheritance-gate.ps1'
$OptBuildManifest = Join-Path $WorkspaceRoot 'Build\Validation\opt-gate\BUILD__build.json'
& $OptGateScript -Mode Capture -Stage build -WorkspaceRoot $WorkspaceRoot -Out $OptBuildManifest
if ($LASTEXITCODE -ne 0) {
  throw "Adopted-optimization inheritance gate FAILED at build (exit=$LASTEXITCODE); see $OptBuildManifest"
}

$exe = Get-Item -LiteralPath (Join-Path $NativeBuild 'YeManCC.exe')
$hash = Get-Sha256 $exe.FullName
Write-Output "BUILD_OK"
Write-Output "Workspace: $WorkspaceRoot"
Write-Output "Web:       $WebBuild"
Write-Output "Native:    $($exe.FullName)"
Write-Output "EXE SHA256: $hash"
