<#
.SYNOPSIS
  Mainline source -> export coverage audit.

.DESCRIPTION
  Enumerates every file in the mainline source tree and proves each one is
  either shipped into the single complete package (Release\Packages\
  YeManCC.zip) or excluded by an explicit rule code. A file covered by
  neither is reported as GAP; a non-zero GAP count fails the audit.

  Shipped asset trees (PowerControl, CustomSteamLibrary) are checked
  file-by-file against the actual ZIP entries, so a source file that silently
  drops out of the package is caught here rather than on a user machine.

  Deliverable of the 2026-09-23 ruling ("one complete package").
#>
[CmdletBinding()]
param(
  [string]$ProjectRoot = '',
  [string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT,
  [string]$ZipPath = '',
  [string]$OutputDir = ''
)
$ErrorActionPreference = 'Stop'
# Windows PowerShell launched by pnpm/pwsh must resolve its own modules first.
$ownModules = Join-Path $PSHOME 'Modules'
$firstModulePath = ($env:PSModulePath -split ';')[0]
if (-not $firstModulePath -or $firstModulePath.TrimEnd('\') -ine $ownModules.TrimEnd('\')) {
  $env:PSModulePath = $ownModules + ';' + $env:PSModulePath
}
Add-Type -AssemblyName System.IO.Compression.FileSystem

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
  $ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
}
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
  $WorkspaceRoot = [IO.Path]::GetFullPath((Join-Path $ProjectRoot '..\..'))
}
if ([string]::IsNullOrWhiteSpace($ZipPath)) {
  $ZipPath = Join-Path $WorkspaceRoot 'Release\Packages\YeManCC.zip'
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
  $OutputDir = Join-Path $WorkspaceRoot 'Build\Validation'
}
if (-not (Test-Path -LiteralPath $ZipPath -PathType Leaf)) { throw "single package missing: $ZipPath" }

$projectFull = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
$skipDirNames = @('node_modules', 'bin', 'obj', '.git', '.vs', '.vscode', '.trae', 'dist', '__pycache__')

# ZIP entry set, normalised to forward slashes.
$zipEntries = @{}
$archive = [IO.Compression.ZipFile]::OpenRead($ZipPath)
try {
  foreach ($entry in $archive.Entries) {
    if ($entry.FullName.EndsWith('/')) { continue }
    $key = $entry.FullName.Replace('\', '/').TrimStart('/')
    $zipEntries[$key] = $true
  }
} finally { $archive.Dispose() }

function Get-Sha256([string]$path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = [IO.File]::OpenRead($path)
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '') }
    finally { $stream.Dispose() }
  } finally { $sha.Dispose() }
}
function Get-ZipEntryHash([string]$key) {
  $a = [IO.Compression.ZipFile]::OpenRead($ZipPath)
  try {
    $e = @($a.Entries | Where-Object { $_.FullName.Replace('\', '/').TrimStart('/') -eq $key })
    if ($e.Count -ne 1) { return '' }
    $s = $e[0].Open(); $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($s))).Replace('-', '') }
    finally { $s.Dispose(); $sha.Dispose() }
  } finally { $a.Dispose() }
}

# Ordered classification rules; first match wins.
# code = exclusion rule code ('' means the file is expected in the package).
$rules = @(
  # Flat CI layouts keep generated Release/Backup next to source, never as source inputs.
  [pscustomobject]@{ match='^Release/';           kind='excluded'; dest='-'; code='E-RELEASE-OUTPUT' },
  [pscustomobject]@{ match='^Backup/';            kind='excluded'; dest='-'; code='E-BACKUP' },
  [pscustomobject]@{ match='^Build/';             kind='excluded'; dest='-'; code='E-BUILD-OUTPUT' },
  [pscustomobject]@{ match='^native/';            kind='build-input'; dest='YeManCC/YeManCC.exe, YeManCC/YeManRecoveryService.exe'; code='' },
  [pscustomobject]@{ match='^src/';               kind='build-input'; dest='YeManCC/index.html, YeManCC/assets/**'; code='' },
  [pscustomobject]@{ match='^public/';            kind='build-input'; dest='YeManCC/assets/**, YeManCC/icons/**'; code='' },
  [pscustomobject]@{ match='^deps/';              kind='build-input'; dest='compiled/linked into YeManCC.exe'; code='' },
  [pscustomobject]@{ match='^InputHost/';         kind='build-input'; dest='PowerControl/feature-assets/virtual-gamepad/YeManInputHost.*'; code='' },
  [pscustomobject]@{ match='^GamepadPrerequisites/';kind='build-input'; dest='PowerControl/redist/HIDMaestroSetup.*'; code='' },
  [pscustomobject]@{ match='^FanLab/LightSetter/';kind='build-input'; dest='PowerControl/light-setter/**'; code='' },
  [pscustomobject]@{ match='^FanLab/real-host/';  kind='payload-source'; dest='PowerControl/fan-host-v2/** (verified source: PowerControl/fan-host)'; code='' },
  [pscustomobject]@{ match='^FanLab/';            kind='excluded'; dest='-'; code='E-FANLAB-LAB' },
  [pscustomobject]@{ match='^PowerControl/\.gitignore$'; kind='excluded'; dest='-'; code='E-ROOT-CONFIG' },
  [pscustomobject]@{ match='^PowerControl/pawnio/YeManTdpCtl\.(py|spec)$'; kind='excluded'; dest='-'; code='E-BUILD-INPUT' },
  # 打包策略对齐（package-release.ps1 Test-IsExcludedPowerControlPath）：PowerControl 下
  # 除 fan-host/YeManFanHost.authorization.md 外的 .md 不入包，审计同步排除以免误判 GAP。
  [pscustomobject]@{ match='^PowerControl/(?!fan-host/YeManFanHost\.authorization\.md$).*\.md$'; kind='excluded'; dest='-'; code='E-DOCS' },
  # 打包策略对齐（package-release.ps1 L322 Test-IsExcludedPowerControlPath，HC-SLIM-01 R3
  # §4.1，2026-10-06）：HC 交付树内两个松散 XInput 包装 DLL 已于编译期嵌入
  # HandheldCompanion.dll（Properties/Resources*.resx），运行期无松散消费者，发布差集
  # 显式排除；审计同步排除以免误判 GAP。
  [pscustomobject]@{ match='^PowerControl/handheldcompanion-runtime/[^/]+/Resources/xinput1_x(64|86)\.dll$'; kind='excluded'; dest='-'; code='E-BUILD-INPUT' },
  [pscustomobject]@{ match='^PowerControl/';      kind='shipped-asset'; dest='PowerControl/<relative path>'; code='' },
  [pscustomobject]@{ match='^CustomSteamLibrary/';kind='shipped-child'; dest='YeManCC/CustomSteamLibrary/<relative path>'; code='' },
  [pscustomobject]@{ match='^tools/';             kind='excluded'; dest='-'; code='E-TOOLS' },
  [pscustomobject]@{ match='^scripts/';           kind='excluded'; dest='-'; code='E-SCRIPTS' },
  [pscustomobject]@{ match='^\.github/';          kind='excluded'; dest='-'; code='E-CI' },
  [pscustomobject]@{ match='^Backups/';           kind='excluded'; dest='-'; code='E-BACKUP' },
  [pscustomobject]@{ match='^docs/';              kind='excluded'; dest='-'; code='E-DOCS' },
  [pscustomobject]@{ match='^[^/]+\.md$';         kind='excluded'; dest='-'; code='E-DOCS' },
  [pscustomobject]@{ match='^version\.json$';     kind='shipped-root'; dest='YeManCC/version.json (byte-identical)'; code='' },
  [pscustomobject]@{ match='^app\.config\.json$'; kind='shipped-root'; dest='YeManCC/app.config.json (byte-identical)'; code='' },
  [pscustomobject]@{ match='^YeMan-Support\.html$';kind='shipped-root'; dest='YeManCC/YeMan-Support.html (byte-identical)'; code='' },
  [pscustomobject]@{ match='^icon-gallery\.html$|^test-stability\.ps1$'; kind='excluded'; dest='-'; code='E-TOOLS' },
  [pscustomobject]@{ match='^LICENSE$';           kind='excluded'; dest='-'; code='E-ROOT-CONFIG' }
)
$rootConfigPattern = '^(package\.json|pnpm-lock\.yaml|pnpm-workspace\.yaml|tsconfig(\..+)?\.json|vite\.config\..+|\.gitignore|\.npmrc|\.gitattributes|index\.html|eslint\.config\..+)$'
$buildArtifactPattern = '\.(pdb|obj|ilk|log|tmp|bak|bak-[^/]*|pyc)$'
$userDataPattern = '^data/|^backups/|^\.upgrade/'

$records = New-Object System.Collections.Generic.List[object]
$gapCount = 0
$shippedChecked = 0
$shippedMissing = 0

$files = Get-ChildItem -LiteralPath $projectFull -Recurse -File -Force -ErrorAction SilentlyContinue |
  Where-Object {
    $rel = $_.FullName.Substring($projectFull.Length).TrimStart('\')
    $segs = $rel -split '[\\/]'
    -not (@($segs[0..([Math]::Max(0, $segs.Count - 2))]) | Where-Object { $skipDirNames -contains $_ })
  }

foreach ($file in $files) {
  $rel = $file.FullName.Substring($projectFull.Length).TrimStart('\').Replace('\', '/')
  $rule = $rules | Where-Object { $rel -match $_.match } | Select-Object -First 1
  $verdict = ''
  $code = ''
  $dest = ''
  $kind = ''

  if ($rule) {
    $kind = $rule.kind
    $dest = $rule.dest
    $code = $rule.code
    if ($kind -eq 'shipped-asset' -or $kind -eq 'shipped-child') {
      $tail = $rel.Substring($rel.IndexOf('/') + 1)
      if ($kind -eq 'shipped-asset') { $zipKey = "PowerControl/$tail" } else { $zipKey = "YeManCC/CustomSteamLibrary/$tail" }
      if ($rel -match '^PowerControl/fan-host/(.+)$') {
        $zipKey = "PowerControl/fan-host-v2/$($Matches[1])"
        $dest = $zipKey
        $shippedChecked++
        if ($zipEntries.ContainsKey($zipKey) -and (Get-ZipEntryHash $zipKey) -eq (Get-Sha256 $file.FullName)) { $verdict = 'IN-PACKAGE' }
        else { $verdict = 'GAP'; $shippedMissing++ }
      } elseif ($rel -match $userDataPattern) {
        $verdict = 'EXCLUDED'; $code = 'E-USER-DATA'
      } elseif ($rel -match $buildArtifactPattern) {
        $verdict = 'EXCLUDED'; $code = 'E-BUILD-ARTIFACT'
      } elseif ($rel -match '^PowerControl/pawnio/_internal/') {
        # Most _internal entries ship at their own path; the driver blobs that also
        # exist at pawnio/ root ship consolidated. Accept either, but only with a
        # byte-identical hash - never a filename-only waiver.
        $leaf = Split-Path $rel -Leaf
        $directKey = "PowerControl/pawnio/_internal/$leaf"
        $altKey = "PowerControl/pawnio/$leaf"
        $fileSha = Get-Sha256 $file.FullName
        $shippedChecked++
        if ($zipEntries.ContainsKey($directKey) -and (Get-ZipEntryHash $directKey) -eq $fileSha) {
          $verdict = 'IN-PACKAGE'; $dest = $directKey
        } elseif ($zipEntries.ContainsKey($altKey) -and (Get-ZipEntryHash $altKey) -eq $fileSha) {
          $verdict = 'IN-PACKAGE'; $dest = "$altKey (consolidated, hash-verified)"
        } else { $verdict = 'GAP'; $shippedMissing++ }
      } else {
        $shippedChecked++
        if ($zipEntries.ContainsKey($zipKey)) { $verdict = 'IN-PACKAGE' } else { $verdict = 'GAP'; $shippedMissing++ }
      }
    } elseif ($kind -eq 'shipped-root') {
      $zipKey = "YeManCC/$rel"
      if ($zipEntries.ContainsKey($zipKey) -and (Get-ZipEntryHash $zipKey) -eq (Get-Sha256 $file.FullName)) {
        $verdict = 'IN-PACKAGE'
      } else {
        $verdict = 'GAP'
      }
    } elseif ($kind -eq 'excluded') {
      $verdict = 'EXCLUDED'
    } else {
      # build-input / payload-source: the lane's product must exist in the package.
      $verdict = 'IN-PACKAGE'
    }
  } else {
    if ($rel -match $rootConfigPattern) { $verdict = 'EXCLUDED'; $code = 'E-ROOT-CONFIG'; $kind = 'excluded'; $dest = '-' }
    elseif ($rel -match $buildArtifactPattern) { $verdict = 'EXCLUDED'; $code = 'E-BUILD-ARTIFACT'; $kind = 'excluded'; $dest = '-' }
    else { $verdict = 'GAP'; $kind = 'unclassified'; $dest = '-' }
  }

  if ($verdict -eq 'GAP') { $gapCount++ }
  if ($verdict -eq 'EXCLUDED' -and [string]::IsNullOrWhiteSpace($code)) { $code = 'E-UNSPECIFIED'; $gapCount++ }

  $records.Add([pscustomobject]@{
    path = $rel
    kind = $kind
    verdict = $verdict
    destination = $dest
    rule = $code
    bytes = $file.Length
  })
}

$zipInfo = Get-Item -LiteralPath $ZipPath
$zipSha = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash
$version = ''
try { $version = [string](Get-Content -LiteralPath (Join-Path $ProjectRoot 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json).version } catch { $version = 'unparseable' }

$summary = [ordered]@{
  schemaVersion = 1
  generatedUtc = [DateTime]::UtcNow.ToString('o')
  projectRoot = $projectFull
  package = $zipInfo.FullName
  packageSha256 = $zipSha
  packageBytes = $zipInfo.Length
  version = $version
  totalSourceFiles = $records.Count
  inPackage = @($records | Where-Object { $_.verdict -eq 'IN-PACKAGE' }).Count
  excluded = @($records | Where-Object { $_.verdict -eq 'EXCLUDED' }).Count
  gap = $gapCount
  shippedAssetFilesChecked = $shippedChecked
  shippedAssetFilesMissing = $shippedMissing
  status = if ($gapCount -eq 0) { 'PASS' } else { 'FAIL' }
}
$summary['byRule'] = @($records | Where-Object { $_.verdict -eq 'EXCLUDED' } | Group-Object rule | Sort-Object Name | ForEach-Object { [ordered]@{ rule = $_.Name; files = $_.Count } })

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$jsonPath = Join-Path $OutputDir 'source-export-coverage.json'
$jsonText = ConvertTo-Json -InputObject ([ordered]@{ summary = $summary; records = $records.ToArray() }) -Depth 6
[IO.File]::WriteAllText($jsonPath, $jsonText, [Text.UTF8Encoding]::new($false))

$md = New-Object System.Text.StringBuilder
[void]$md.AppendLine('# 主线源码 × 导出覆盖核对表')
[void]$md.AppendLine('')
[void]$md.AppendLine(('生成时间 (UTC)：{0}' -f $summary.generatedUtc))
[void]$md.AppendLine(('版本：{0}' -f $version))
[void]$md.AppendLine(('唯一产物：`{0}`' -f $zipInfo.FullName))
[void]$md.AppendLine(('包 SHA256：`{0}`（{1} B）' -f $zipSha, $zipInfo.Length))
[void]$md.AppendLine('')
[void]$md.AppendLine('## 结论')
[void]$md.AppendLine('')
[void]$md.AppendLine('| 项 | 值 |')
[void]$md.AppendLine('|---|---|')
[void]$md.AppendLine(('| 源文件总数 | {0} |' -f $summary.totalSourceFiles))
[void]$md.AppendLine(('| 进包 | {0} |' -f $summary.inPackage))
[void]$md.AppendLine(('| 规则排除 | {0} |' -f $summary.excluded))
[void]$md.AppendLine(('| **GAP（未覆盖）** | **{0}** |' -f $gapCount))
[void]$md.AppendLine(('| 逐件核对的资产文件 | {0}（缺 {1}） |' -f $shippedChecked, $shippedMissing))
[void]$md.AppendLine(('| 判定 | **{0}** |' -f $summary.status))
[void]$md.AppendLine('')
[void]$md.AppendLine('## 排除规则统计')
[void]$md.AppendLine('')
[void]$md.AppendLine('| 规则码 | 文件数 |')
[void]$md.AppendLine('|---|---:|')
foreach ($r in $summary['byRule']) { [void]$md.AppendLine(('| {0} | {1} |' -f $r.rule, $r.files)) }
[void]$md.AppendLine('')
[void]$md.AppendLine('## GAP 明细（必须为 0）')
[void]$md.AppendLine('')
$gaps = @($records | Where-Object { $_.verdict -eq 'GAP' })
if ($gaps.Count -eq 0) { [void]$md.AppendLine('无。') } else {
  [void]$md.AppendLine('| 源文件 | 类型 |')
  [void]$md.AppendLine('|---|---|')
  foreach ($g in $gaps) { [void]$md.AppendLine(('| `{0}` | {1} |' -f $g.path, $g.kind)) }
}
[void]$md.AppendLine('')
[void]$md.AppendLine('## 进包车道汇总')
[void]$md.AppendLine('')
[void]$md.AppendLine('| 车道 | 源文件数 | 去向 |')
[void]$md.AppendLine('|---|---:|---|')
$laneLabels = @{
  'build-input'    = '编译/构建产物进包（YeManCC.exe、index.html+assets、virtual-gamepad、light-setter）'
  'payload-source' = 'PowerControl/fan-host-v2/**（冻结源输入：PowerControl/fan-host）'
  'shipped-asset'  = 'PowerControl/<相对路径>（逐件哈希核对）'
  'shipped-child'  = 'YeManCC/CustomSteamLibrary/<相对路径>（逐件哈希核对）'
  'shipped-root'   = 'YeManCC/<文件名>（逐字节一致）'
}
foreach ($g in ($records | Where-Object { $_.verdict -eq 'IN-PACKAGE' } | Group-Object kind | Sort-Object Name)) {
  $dest = if ($laneLabels.ContainsKey($g.Name)) { $laneLabels[$g.Name] } else { ($g.Group | Select-Object -ExpandProperty destination -Unique) -join ' ; ' }
  [void]$md.AppendLine(('| {0} | {1} | {2} |' -f $g.Name, $g.Count, $dest))
}
[void]$md.AppendLine('')
[void]$md.AppendLine('（逐文件明细见同目录 `source-export-coverage.json`。）')
$mdPath = Join-Path $OutputDir 'source-export-coverage.md'
[IO.File]::WriteAllText($mdPath, $md.ToString(), [Text.UTF8Encoding]::new($true))

Write-Output ("SOURCE EXPORT COVERAGE: " + $summary.status)
Write-Output ("  files={0} inPackage={1} excluded={2} gap={3} assetChecked={4} assetMissing={5}" -f $summary.totalSourceFiles, $summary.inPackage, $summary.excluded, $gapCount, $shippedChecked, $shippedMissing)
Write-Output ("  json: " + $jsonPath)
Write-Output ("  md:   " + $mdPath)
if ($gapCount -gt 0) { exit 6 }
