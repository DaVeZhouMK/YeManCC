<#
opt-inheritance-gate.ps1 - adopted-optimization inheritance gate + export manifest.
ASCII-ONLY (PS 5.1 reads BOM-less files as ANSI; CJK bytes can swallow the next line).
PowerShell 5.1 compatible (no -AsHashtable, no 3-arg Join-Path).

Sits ON TOP of opt-manifest.ps1 and reuses its ideas (marker/anchor registry, class
digests, source-tree digest) but sources the registry + class rules + generated-file
rules from a MIGRATABLE source-tree config: tools/opt-baseline.json (no machine
absolute Docs path dependency).

Design guarantees required by the release ruling:
  * each build binds ITS OWN source-tree digest + toolchain + dependency + product
    hashes into a captured BuildManifest; downstream stages are checked against THAT
    manifest's hashes. No fixed "today" exe hash is baked into the baseline, so a
    future legitimate feature change may change the hash without tripping the gate.
  * the gate NEVER re-scans a directory to fabricate a fresh all-OK manifest that
    would hide a mix of generations: Gate mode REQUIRES a pre-existing BuildManifest
    and compares disk artifacts against it.
  * the two packaging-generated files (version.json / YeMan-Support.html) are
    validated by CONTENT MATCH against their source-root file (sha256 == source) and
    their hash is recorded; never ignored by filename, and the whole frontend/config
    class is never exempted.

Modes:
  Capture -Stage <build|package|package-update|release|install> [-Name id] [-OutDir d] [-Out f]
  Verify  -Manifest <json>
  Diff    -Base <json> -Cand <json> [-ExpectAdded a,b] [-ExpectChanged cls] [-ExpectSame cls]
  Gate    -Stage <package|release|install> -BuildManifest <build.json>

Exit codes: 0 PASS | 2 bad args/file | 3 missing build manifest | 6 capture/verify fail
          | 7 diff assertion fail | 8 gate fail (block packaging/release).
#>
[CmdletBinding()]
param(
  [ValidateSet('Capture','Verify','Diff','Gate','ReleasePackage')][Parameter(Mandatory=$true)][string]$Mode,
  [ValidateSet('build','package','package-update','release','install')][string]$Stage = '',
  [string]$Name = '',
  [string]$OutDir = '',
  [string]$Out = '',
  [string]$Manifest = '',
  [string]$Base = '',
  [string]$Cand = '',
  [string]$BuildManifest = '',
  [string]$WorkspaceRoot = '',
  [string]$ProjectRoot = '',
  [string]$Zip = '',
  [string]$ZipRoot = '',
  [string[]]$IncludeRoots = @(),
  [string]$AppDirOverride = '',
  [string]$PcDirOverride = '',
  [string]$WebDirOverride = ''
)
$ErrorActionPreference = 'Stop'

# ---- module-path self-heal -----------------------------------------------------------
# Same measured remedy as tools/ymcc_sim/lib/sim_core.ps1 (2026-09-22): when this Windows
# PowerShell 5.1 process is launched through node/pnpm (as `pnpm run build` does, and as CI
# does under shell: pwsh) it inherits PowerShell 7's PSModulePath. The leading PowerShell-7
# entries shadow Microsoft.PowerShell.Utility, so autoloading of Get-FileHash fails with
# "CommandNotFoundException" and the gate aborts the build. Put this host's own module dir
# first. Idempotent.
$gateHomeModules = Join-Path $PSHOME 'Modules'
$gateFirstEntry = ($env:PSModulePath -split ';')[0]
if (-not $gateFirstEntry -or ($gateFirstEntry.TrimEnd('\') -ine $gateHomeModules.TrimEnd('\'))) {
  $env:PSModulePath = $gateHomeModules + ';' + $env:PSModulePath
}

# ---- defensive list split (powershell -File flattens "a,b,c" into ONE string) --------
function Split-List([string[]]$v) {
  if ($v.Count -eq 1 -and $v[0] -match ',') {
    return @($v[0] -split ',' | ForEach-Object { $_.Trim().Trim("'").Trim('"') } | Where-Object { $_ -ne '' })
  }
  return $v
}
$ExpectAdded = Split-List $ExpectAdded
$ExpectChanged = Split-List $ExpectChanged
$ExpectSame = Split-List $ExpectSame
$IncludeRoots = Split-List $IncludeRoots

# ---- path resolution (migratable: relative to this script, not to g:\...) -----------
$Here = $PSScriptRoot                                    # .../YeManCC-source/YeManCC/tools
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }  # .../YeManCC-source/YeManCC
if (-not $WorkspaceRoot) { $WorkspaceRoot = [IO.Path]::GetFullPath((Join-Path $ProjectRoot '..\..')) }  # .../Mainline
if (-not $OutDir) { $OutDir = Join-Path $WorkspaceRoot 'Build\Validation\opt-gate' }

# ---- load migratable baseline (PS 5.1: JSON -> nested hashtable by hand) ------------
function ConvertTo-HashtableDeep($o) {
  if ($null -eq $o) { return $null }
  if ($o -is [string]) { return $o }
  if ($o -is [bool] -or $o -is [int] -or $o -is [long] -or $o -is [double] -or $o -is [decimal]) { return $o }
  if ($o -is [System.Collections.IDictionary]) {
    $h = @{}
    foreach ($k in $o.Keys) { $h[$k] = ConvertTo-HashtableDeep $o[$k] }
    return $h
  }
  if ($o -is [System.Collections.IEnumerable]) {
    $arr = @(foreach ($i in $o) { ConvertTo-HashtableDeep $i })
    return ,$arr
  }
  if ($o -is [System.Management.Automation.PSCustomObject]) {
    $h = @{}
    foreach ($p in $o.PSObject.Properties) { $h[$p.Name] = ConvertTo-HashtableDeep $p.Value }
    return $h
  }
  return $o
}
$cfgPath = Join-Path $Here 'opt-baseline.json'
if (-not (Test-Path -LiteralPath $cfgPath -PathType Leaf)) { Write-Output ('ABORT: baseline config missing: ' + $cfgPath); exit 2 }
$Opt = ConvertTo-HashtableDeep (Get-Content -LiteralPath $cfgPath -Raw -Encoding UTF8 | ConvertFrom-Json)
$optRules = @($Opt['optimizations'])
$adopted = @($optRules | Where-Object { $_['status'] -eq 'adopted' } | ForEach-Object { $_['name'] })
$classRules = $Opt['artifacts']['classRules']
$generated = @($Opt['packagingGeneratedFiles'])
$mustMatch = @($Opt['mustMatchClasses'])
$stageLayouts = $Opt['stageLayouts']
$validStages = @('build','package','package-update','release','install')

function Write-JsonFile([string]$path, [object]$o) {
  $dir = Split-Path $path -Parent
  if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
  [System.IO.File]::WriteAllText($path, ($o | ConvertTo-Json -Depth 14), [System.Text.UTF8Encoding]::new($false))
}
# Credentials are staged to a sibling temp file and swapped in atomically, so a
# crash can never leave a half-written credential in place of the last good one.
function Write-JsonAtomic([string]$path, [object]$o) {
  $dir = Split-Path $path -Parent
  if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
  $suffix = ([Guid]::NewGuid().ToString('N').Substring(0, 8))
  $tmp = $path + '.tmp-' + $suffix
  [System.IO.File]::WriteAllText($tmp, ($o | ConvertTo-Json -Depth 14), [System.Text.UTF8Encoding]::new($false))
  if (Test-Path -LiteralPath $path -PathType Leaf) {
    # PS 5.1 passes $null as '' for File.Replace's backup arg, so use a real swap path
    $swap = $path + '.swap-' + $suffix
    try {
      [System.IO.File]::Replace($tmp, $path, $swap)
    } finally {
      if (Test-Path -LiteralPath $swap) { Remove-Item -LiteralPath $swap -Force -ErrorAction SilentlyContinue }
    }
  } else {
    [System.IO.File]::Move($tmp, $path)
  }
  if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
}
# A failed run must never overwrite the last successful credential.
function Get-FailurePath([string]$outPath) {
  $dir = Split-Path $outPath -Parent
  return (Join-Path $dir ([IO.Path]::GetFileNameWithoutExtension($outPath) + '.failed.json'))
}
function Read-Json([string]$path) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
  return (Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json)
}
function Hash-File([string]$path) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return '' }
  return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}
function Contains-Bytes([string]$path, [string]$marker) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
  $text = ([System.Text.Encoding]::GetEncoding(28591)).GetString([System.IO.File]::ReadAllBytes($path))
  return ($text.IndexOf($marker) -ge 0)
}
function Contains-Text([string]$path, [string]$text) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
  return ([System.IO.File]::ReadAllText($path)).IndexOf($text) -ge 0
}
function Get-Sha256Hex([string]$text) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try { $h = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($text)) } finally { $sha.Dispose() }
  return (($h | ForEach-Object { $_.ToString('x2') }) -join '')
}
function Short-Hash([string]$h) {
  if (-not $h) { return '<none>' }
  if ($h.Length -ge 16) { return $h.Substring(0, 16) }
  return $h
}
# Ordinal (culture-invariant) sort by an ASCII key string. Sort-Object is
# culture-sensitive, so identical data can digest differently under zh-CN vs
# en-US UI culture; every digest-feeding sort MUST be ordinal-stable.
function Sort-Ordinal([object[]]$items, [scriptblock]$keyExpr) {
  if (-not $items -or $items.Count -lt 2) { return $items }
  $n = $items.Count
  $keys = New-Object 'System.Collections.Generic.List[string]'
  for ($i = 0; $i -lt $n; $i++) { $keys.Add([string]($keyExpr.Invoke($items[$i])[0])) }
  $idx = New-Object 'int[]' $n
  for ($i = 0; $i -lt $n; $i++) { $idx[$i] = $i }
  $kArr = $keys.ToArray()
  [Array]::Sort($kArr, $idx, [System.StringComparer]::Ordinal)
  $out = New-Object 'System.Collections.Generic.List[object]'
  foreach ($i in $idx) { $out.Add($items[$i]) }
  return [object[]]$out.ToArray()
}
function Sort-OrdinalStrings([string[]]$list) {
  if (-not $list -or $list.Count -lt 2) { return $list }
  $a = [string[]]$list
  [Array]::Sort($a, [System.StringComparer]::Ordinal)
  return $a
}

# ---- stage layout + artifact resolution --------------------------------------------
function Join-StagePath([string]$base, [string]$rel) {
  $p = $rel -replace '/', '\'
  if ($p -match '^[A-Za-z]:\\' -or $p.StartsWith('\\')) { return $p }   # already absolute (install lane)
  return (Join-Path $base $p)
}
function Get-ResolvedStage([string]$stage) {
  $def = $Opt['stageLayouts'][$stage]
  $app = Join-StagePath $WorkspaceRoot $def['app']
  $pc  = Join-StagePath $WorkspaceRoot $def['pc']
  $web = Join-StagePath $WorkspaceRoot $def['web']
  if ($AppDirOverride) { $app = $AppDirOverride }
  if ($PcDirOverride)  { $pc  = $PcDirOverride }
  if ($WebDirOverride) { $web = $WebDirOverride }
  return @{ app = $app; pc = $pc; web = $web; stage = $stage }
}
# Binary-marker probe targets. A stage that legitimately ships no Host/SDK binary
# (release / package-update carry only flag files for feature-assets) falls back to
# the build artifact for the MARKER probe; source anchors are always probed in the
# working tree. Per-artifact hash equality is still enforced for whatever IS present.
function Resolve-ProbeTargets([string]$nativeExe, [string]$hostDll) {
  $bn = Join-Path $WorkspaceRoot 'Build\App\Native\YeManCC.exe'
  $bh = Join-Path $WorkspaceRoot 'Build\App\Native\InputHost\YeManInputHost.dll'
  if (-not (Test-Path -LiteralPath $nativeExe -PathType Leaf)) { $nativeExe = $bn }
  if (-not (Test-Path -LiteralPath $hostDll -PathType Leaf)) { $hostDll = $bh }
  return @{ native = $nativeExe; host = $hostDll }
}
function Resolve-Rule([hashtable]$stageDir, [string]$rule, [string]$cls) {
  $rule = $rule -replace '/', '\'
  $tag = ''; $rel = $rule
  if ($rule.StartsWith('@')) { $parts = $rule -split '\\', 2; $tag = $parts[0].Substring(1); $rel = $parts[1] }
  $baseDir = $stageDir[$tag]
  if (-not $baseDir) { throw ('Resolve-Rule: unknown tag in rule: ' + $rule) }
  $rows = [System.Collections.Generic.List[object]]::new()
  if ($rel -like '*\**') {
    $dir = Join-Path $baseDir ($rel.Substring(0, $rel.Length - 3))
    if (Test-Path -LiteralPath $dir -PathType Container) {
      foreach ($f in (Sort-Ordinal @(Get-ChildItem -LiteralPath $dir -Recurse -File -Force -ErrorAction SilentlyContinue) { param($x) [string]$x.FullName })) {
        $rows.Add([pscustomobject]@{ cls = $cls; rel = $f.FullName.Substring($baseDir.Length + 1); path = $f.FullName; size = $f.Length; sha256 = (Hash-File $f.FullName) })
      }
    }
  } else {
    $full = Join-Path $baseDir $rel
    # HC-SLIM-01: route two REQUIRED logical sdk-dll artifacts to their pinned
    # authoritative copy. Keep cls/rel/sha and class digest identical across
    # build and thin production; this is not an absent-artifact exemption.
    $routes = $Opt['sharedRuntimeRoutes']
    if ($null -ne $routes -and $cls -eq 'sdk-dll' -and $tag -eq 'pc' -and
        @($routes['stages']) -contains $stageDir['stage'] -and $routes['sha256'].ContainsKey($rel)) {
      $features = Split-Path -Parent $baseDir
      $power = Split-Path -Parent $features
      if ((Split-Path -Leaf $baseDir) -ine 'virtual-gamepad' -or
          (Split-Path -Leaf $features) -ine 'feature-assets' -or
          (Split-Path -Leaf $power) -ine 'PowerControl') { throw 'Shared sdk artifact route requires the production PowerControl layout.' }
      if (Test-Path -LiteralPath $full) { throw ('Thin production contains unexpected local duplicate: ' + $rel) }
      $full = Join-Path $power ('handheldcompanion-runtime\' + [string]$routes['runtimeId'] + '\' + $rel)
      $probe = $full
      while ($probe -and $probe.Length -ge $power.Length) {
        if ((Test-Path -LiteralPath $probe) -and (Get-Item -LiteralPath $probe -Force).Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) {
          throw ('Shared sdk artifact route refuses reparse point: ' + $probe)
        }
        $probe = Split-Path -Parent $probe
      }
      if ((Test-Path -LiteralPath $full -PathType Leaf) -and (Hash-File $full) -ine $routes['sha256'][$rel]) {
        throw ('Pinned shared sdk artifact hash mismatch: ' + $rel)
      }
    }
    $exists = Test-Path -LiteralPath $full -PathType Leaf
    $sz = -1; if ($exists) { $sz = (Get-Item -LiteralPath $full).Length }
    $rows.Add([pscustomobject]@{ cls = $cls; rel = $rel; path = $full; size = $sz; sha256 = (Hash-File $full) })
  }
  return [object[]]$rows
}
function Get-StageArtifacts([hashtable]$stageDir) {
  $rows = [System.Collections.Generic.List[object]]::new()
  foreach ($cls in $classRules.Keys) {
    foreach ($rule in $classRules[$cls]) {
      foreach ($r in (Resolve-Rule $stageDir $rule ([string]$cls))) { $rows.Add($r) }
    }
  }
  return [object[]]$rows
}
function Get-ClassDigest([object[]]$rows, [string[]]$excludeKeys) {
  # Serialize first, then ordinal-sort the lines. Sorting the whole 'cls|rel:sha'
  # line ordinally is equivalent to an ordinal (cls, rel) sort, and is stable
  # regardless of the host UI culture.
  $lines = New-Object 'System.Collections.Generic.List[string]'
  foreach ($r in $rows) {
    if (-not $r.sha256) { continue }
    if ($excludeKeys -contains ($r.cls + '|' + ($r.rel -replace '\\','/'))) { continue }
    $lines.Add(([string]$r.cls) + '|' + ([string]$r.rel) + ':' + ([string]$r.sha256))
  }
  $arr = [string[]]$lines.ToArray()
  [Array]::Sort($arr, [System.StringComparer]::Ordinal)
  $sb = [System.Text.StringBuilder]::new()
  foreach ($l in $arr) { [void]$sb.Append($l); [void]$sb.Append("`n") }
  return Get-Sha256Hex $sb.ToString()
}
# generated-file keys as "class|rel-with-forward-slashes"
$genKeys = @(foreach ($g in $generated) { $g['class'] + '|' + ($g['relativePath'] -replace '\\','/') })
function Get-GenKey([string]$cls, [string]$rel) { return $cls + '|' + ($rel -replace '\\','/') }

function Get-OptStatus([string]$nativeExe, [string]$hostDll) {
  $list = [System.Collections.Generic.List[object]]::new()
  foreach ($r in $optRules) {
    $target = $nativeExe; if ($r['probeTarget'] -eq 'host') { $target = $hostDll }
    $markerOk = $true; $markers = @()
    foreach ($m in $r['markers']) {
      $f = Contains-Bytes $target $m
      $markers += [pscustomobject]@{ marker = $m; probe = $r['probeTarget']; foundInProbe = $f }
      if (-not $f) { $markerOk = $false }
    }
    $anchorOk = $true; $anchors = @()
    foreach ($a in $r['anchors']) {
      $found = Contains-Text (Join-Path $ProjectRoot ($a['file'] -replace '/','\')) $a['text']
      $anchors += [pscustomobject]@{ file = $a['file']; text = $a['text']; found = $found }
      if (-not $found) { $anchorOk = $false }
    }
    $list.Add([pscustomobject]@{
      name = $r['name']; status = $r['status']; evidence = $r['evidence']
      markerOk = $markerOk; anchorOk = $anchorOk; present = ($markerOk -and $anchorOk)
      markers = $markers; anchors = $anchors
    })
  }
  return [object[]]$list
}

function Get-SourceScopeFiles() {
  $skipDirs = @('node_modules','bin','obj','dist','Build','Backups','.git','.github','packages','.vs')
  $skipExt = @('.obj','.exe','.dll','.pdb','.log','.ilk','.exp','.lib','.res','.zip','.7z')
  $out = [System.Collections.Generic.List[object]]::new()
  foreach ($scope in @($Opt['versionCompatibility']['sourceScope'])) {
    $p = Join-Path $ProjectRoot (($scope -replace '/','\'))
    if (-not (Test-Path -LiteralPath $p -PathType Container)) { continue }
    foreach ($f in (Get-ChildItem -LiteralPath $p -Recurse -File -Force -ErrorAction SilentlyContinue)) {
      $rel = $f.FullName.Substring($ProjectRoot.Length + 1)
      $skip = $false
      foreach ($s in ($rel -split '\\')) { if ($skipDirs -contains $s) { $skip = $true; break } }
      if ($skip) { continue }
      if ($skipExt -contains $f.Extension.ToLower()) { continue }
      if ($f.Name -match '\.bak' -or $f.Name -match '(~$|\.orig$|\.rej$)') { continue }
      $out.Add([pscustomobject]@{ rel = $rel; path = $f.FullName; size = $f.Length })
    }
  }
  $sorted = Sort-Ordinal @($out.ToArray()) { param($x) [string]$x.rel }
  return [object[]]$sorted
}
function Get-TreeDigest([object[]]$entries) {
  $sb = [System.Text.StringBuilder]::new()
  foreach ($e in $entries) { [void]$sb.Append($e.rel); [void]$sb.Append(':'); [void]$sb.Append((Hash-File $e.path)); [void]$sb.Append("`n") }
  return Get-Sha256Hex $sb.ToString()
}
function Get-ToolchainInfo {
  $vsWhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
  $vsPath = ''
  if (Test-Path -LiteralPath $vsWhere -PathType Leaf) { $vsPath = (& $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) -join '' }
  $clVer = ''; $linkVer = ''
  if ($vsPath) {
    $vcRoot = Join-Path $vsPath 'VC\Tools\MSVC'
    if (Test-Path -LiteralPath $vcRoot) {
      $vcVer = (Get-ChildItem -LiteralPath $vcRoot -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1).Name
      $cl = Join-Path $vcRoot ($vcVer + '\bin\Hostx64\x64\cl.exe')
      $lk = Join-Path $vcRoot ($vcVer + '\bin\Hostx64\x64\link.exe')
      if (Test-Path -LiteralPath $cl) { $clVer = (Get-Item -LiteralPath $cl).VersionInfo.FileVersion }
      if (Test-Path -LiteralPath $lk) { $linkVer = (Get-Item -LiteralPath $lk).VersionInfo.FileVersion }
    }
  }
  $jsonHpp = Join-Path $ProjectRoot 'deps\json\json.hpp'
  return [pscustomobject]@{ vsPath = $vsPath; clVersion = $clVer; linkVersion = $linkVer
    compileFlags = $Opt['versionCompatibility']['compileFlags']
    jsonHppSha256 = (Hash-File $jsonHpp); jsonHppSize = (Get-Item -LiteralPath $jsonHpp -ErrorAction SilentlyContinue).Length }
}
function Get-SdkLockCheck([string]$corePath) {
  $const = [string]$Opt['versionCompatibility']['sdkLockSourceConstant']
  $mainCpp = Join-Path $ProjectRoot 'native\main.cpp'
  $lock = ''; $constantFound = $false
  if (Contains-Text $mainCpp $const) {
    $m = [regex]::Match([System.IO.File]::ReadAllText($mainCpp), [regex]::Escape($const) + '\s*=\s*"([0-9a-fA-F]{64})"')
    if ($m.Success) { $lock = $m.Groups[1].Value.ToLower(); $constantFound = $true }
  }
  $coreFound = Test-Path -LiteralPath $corePath -PathType Leaf
  $hash = ''; if ($coreFound) { $hash = (Hash-File $corePath).ToLower() }
  return [pscustomobject]@{ lockConstant = $lock; constantFound = $constantFound
    corePath = $corePath; coreFound = $coreFound; coreSha256 = $hash
    match = ($constantFound -and $coreFound -and $lock -eq $hash) }
}
# STRICT sdk-lock evaluation shared by Capture / Verify / Gate.
# A missing constant, an unparseable constant, a missing core file and a hash
# mismatch are ALL failures (a stage that legitimately ships no SDK core is the
# only exemption, and that is derived from the stage expectation table).
function Test-SdkLockStrict($sdkLock, [string]$stage) {
  $out = [System.Collections.Generic.List[string]]::new()
  if (-not $Opt['versionCompatibility']['requireSdkLockMatch']) { return [object[]]$out }
  $ea = Get-ExpectedAbsent $stage
  if ($ea.classes -contains 'sdk-dll') { return [object[]]$out }
  if (-not $sdkLock.constantFound) {
    $out.Add('sdk lock constant missing or unparseable in native/main.cpp: ' + [string]$Opt['versionCompatibility']['sdkLockSourceConstant'])
  }
  if (-not $sdkLock.coreFound) { $out.Add('sdk lock core file missing: ' + $sdkLock.corePath) }
  if ($sdkLock.constantFound -and $sdkLock.coreFound -and -not $sdkLock.match) {
    $out.Add('sdk lock mismatch: constant=' + (Short-Hash $sdkLock.lockConstant) + ' core=' + (Short-Hash $sdkLock.coreSha256))
  }
  return [object[]]$out
}
# Canonical digest over every rule-bearing baseline section. If the rules change
# after a capture, the capture is no longer a credential (re-capture required).
function Get-RulesDigest {
  $sb = [System.Text.StringBuilder]::new()
  [void]$sb.Append('baseline='); [void]$sb.Append([string]$Opt['baselineName']); [void]$sb.Append("`n")
  foreach ($o in (Sort-Ordinal @($optRules) { param($x) [string]$x['name'] })) {
    [void]$sb.Append('opt='); [void]$sb.Append([string]$o['name']); [void]$sb.Append('|'); [void]$sb.Append([string]$o['status']); [void]$sb.Append('|'); [void]$sb.Append([string]$o['probeTarget'])
    foreach ($mk in (Sort-OrdinalStrings @(@($o['markers']) | ForEach-Object { [string]$_ }))) { [void]$sb.Append('|m:'); [void]$sb.Append([string]$mk) }
    foreach ($an in (Sort-Ordinal @($o['anchors']) { param($x) ([string]$x['file'] + '=' + [string]$x['text']) })) {
      [void]$sb.Append('|a:'); [void]$sb.Append([string]$an['file']); [void]$sb.Append('='); [void]$sb.Append([string]$an['text'])
    }
    [void]$sb.Append("`n")
  }
  foreach ($c in (Sort-OrdinalStrings @($classRules.Keys))) {
    [void]$sb.Append('cls='); [void]$sb.Append([string]$c)
    foreach ($r in (Sort-OrdinalStrings @($classRules[$c] | ForEach-Object { [string]$_ }))) { [void]$sb.Append('|'); [void]$sb.Append([string]$r) }
    [void]$sb.Append("`n")
  }
  [void]$sb.Append('mustMatch='); [void]$sb.Append(((Sort-OrdinalStrings @($mustMatch)) -join ',')); [void]$sb.Append("`n")
  foreach ($g in (Sort-Ordinal @($generated) { param($x) [string]$x['relativePath'] })) {
    [void]$sb.Append('gen='); [void]$sb.Append([string]$g['class']); [void]$sb.Append('|'); [void]$sb.Append([string]$g['relativePath']); [void]$sb.Append('|'); [void]$sb.Append([string]$g['generationRule']); [void]$sb.Append("`n")
  }
  foreach ($s in (Sort-OrdinalStrings @($stageLayouts.Keys))) {
    [void]$sb.Append('layout='); [void]$sb.Append([string]$s); [void]$sb.Append('|'); [void]$sb.Append([string]$stageLayouts[$s]['app']); [void]$sb.Append('|'); [void]$sb.Append([string]$stageLayouts[$s]['pc']); [void]$sb.Append('|'); [void]$sb.Append([string]$stageLayouts[$s]['web']); [void]$sb.Append("`n")
  }
  foreach ($s in (Sort-OrdinalStrings @($Opt['stageExpect'].Keys))) {
    [void]$sb.Append('expect='); [void]$sb.Append([string]$s); [void]$sb.Append('|')
    [void]$sb.Append(((Sort-OrdinalStrings @(@($Opt['stageExpect'][$s]['absentClasses']) | ForEach-Object { [string]$_ })) -join ',')); [void]$sb.Append('|')
    [void]$sb.Append(((Sort-OrdinalStrings @(@($Opt['stageExpect'][$s]['absentRel']) | ForEach-Object { [string]$_ })) -join ',')); [void]$sb.Append("`n")
  }
  $vc = $Opt['versionCompatibility']
  [void]$sb.Append('sdkLock='); [void]$sb.Append([string]$vc['sdkLockSourceConstant']); [void]$sb.Append('|'); [void]$sb.Append([string]$vc['sdkLockFile']); [void]$sb.Append('|'); [void]$sb.Append([string]$vc['requireSdkLockMatch']); [void]$sb.Append("`n")
  [void]$sb.Append('flags='); [void]$sb.Append(((Sort-OrdinalStrings @(@($vc['compileFlags']) | ForEach-Object { [string]$_ })) -join ',')); [void]$sb.Append("`n")
  [void]$sb.Append('scope='); [void]$sb.Append(((Sort-OrdinalStrings @(@($vc['sourceScope']) | ForEach-Object { [string]$_ })) -join ',')); [void]$sb.Append("`n")
  return Get-Sha256Hex $sb.ToString()
}
# Required-artifact check shared by Capture / Verify / Gate: every class rule that
# is not legitimately absent for this stage must exist with a real hash.
function Test-RequiredArtifacts([string]$stage, [object[]]$rows) {
  $out = [System.Collections.Generic.List[string]]::new()
  $ea = Get-ExpectedAbsent $stage
  if (@($rows).Count -eq 0) { $out.Add('no artifacts resolved for stage ' + $stage + ' (an empty artifact set is never valid)'); return [object[]]$out }
  foreach ($r in $rows) {
    if ($ea.classes -contains $r.cls) { continue }
    if ($ea.rel -contains ($r.cls + '|' + $r.rel)) { continue }
    if (-not $r.sha256) { $out.Add('required artifact missing: ' + $r.cls + '/' + $r.rel) }
  }
  return [object[]]$out
}
# Manifest shape / type / stage / required-field validation. Rejects a failed
# capture and an empty artifact set outright: neither is ever a credential.
function Assert-ManifestShape($m, [string]$path) {
  $out = [System.Collections.Generic.List[string]]::new()
  if ($null -eq $m) { $out.Add('manifest unreadable or empty: ' + $path); return [object[]]$out }
  if ([int]$m.schema -ne 2) { $out.Add('manifest schema != 2 (got ' + [string]$m.schema + ')') }
  if ([string]$m.mode -ne 'capture') { $out.Add('manifest mode != capture (got ' + [string]$m.mode + ')') }
  if ([string]$m.validationStatus -ne 'PASS') { $out.Add('manifest validationStatus != PASS (got ' + [string]$m.validationStatus + '); a failed capture is not a credential') }
  if ($validStages -notcontains [string]$m.stage) { $out.Add('manifest stage invalid: ' + [string]$m.stage) }
  if (-not $m.baselineSha256) { $out.Add('manifest baselineSha256 missing') }
  if (-not $m.rulesSha256) { $out.Add('manifest rulesSha256 missing') }
  if (-not $m.sourceTreeSha256) { $out.Add('manifest sourceTreeSha256 missing') }
  if (-not $m.capturedAt) { $out.Add('manifest capturedAt missing') }
  $arts = @($m.artifacts)
  if ($arts.Count -eq 0) { $out.Add('manifest artifacts empty (never a credential)') }
  foreach ($a in $arts) { if (-not $a.cls -or -not $a.rel) { $out.Add('manifest artifact row missing cls/rel'); break } }
  $opts = @($m.optimizations)
  if ($opts.Count -eq 0) { $out.Add('manifest optimizations empty') }
  foreach ($o in $opts) { if (-not $o.present) { $out.Add('manifest records a non-present optimization: ' + [string]$o.name); break } }
  $ad = @($m.adoptedOptimizations)
  if ((@($ad | Sort-Object) -join ',') -ne (@($adopted | Sort-Object) -join ',')) { $out.Add('manifest adoptedOptimizations != current baseline adopted set') }
  return [object[]]$out
}
# Config identity: the capture must have been produced by THIS baseline content
# and THIS rule set. A missing/changed config is rejected, never defaulted to pass.
function Test-ConfigIdentity($m) {
  $out = [System.Collections.Generic.List[string]]::new()
  $curCfg = Hash-File $cfgPath
  $curRules = Get-RulesDigest
  if ([string]$m.baselineSha256 -ne $curCfg) {
    $out.Add('baseline config file changed since capture: manifest=' + (Short-Hash $m.baselineSha256) + ' current=' + (Short-Hash $curCfg) + ' (re-capture required)')
  }
  if ([string]$m.rulesSha256 -ne $curRules) {
    $out.Add('baseline rules changed since capture: manifest=' + (Short-Hash $m.rulesSha256) + ' current=' + (Short-Hash $curRules) + ' (re-capture required)')
  }
  return [object[]]$out
}
# returns per-generated-file record; adds failures for content mismatch (skip a stage
# where the file is legitimately expected-absent, e.g. version.json at build)
function Test-GeneratedFiles([string]$stage, [object[]]$rows, $list) {
  $ea = Get-ExpectedAbsent $stage
  $rec = [System.Collections.Generic.List[object]]::new()
  foreach ($g in $generated) {
    $rel = [string]$g['relativePath']; $cls = [string]$g['class']
    $key = Get-GenKey $cls $rel
    $expectedAbsent = ($ea.rel -contains $key)
    $candRow = @($rows | Where-Object { $_.cls -eq $cls -and ($_.rel -replace '\\','/') -eq ($rel -replace '\\','/') })
    $candPath = ''; if ($candRow.Count -gt 0) { $candPath = $candRow[0].path }
    $srcPath = Join-Path $ProjectRoot ($rel -replace '/','\')
    $h = (Hash-File $candPath); $sh = (Hash-File $srcPath)
    $matches = ($h -ne '' -and $h -eq $sh)
    if ($candPath -and -not $expectedAbsent -and ([string]$g['generationRule']) -like 'byte-identical*' -and -not $matches) {
      $list.Add('generated file NOT byte-identical to source: ' + $rel + ' (stage=' + $h + ' src=' + $sh + ')')
    }
    $rec.Add([pscustomobject]@{ rel = $rel; class = $cls; stageSha256 = $h; sourceSha256 = $sh; matchesSource = $matches; expectedAbsent = $expectedAbsent })
  }
  return [object[]]$rec
}
function Get-ExpectedAbsent([string]$stage) {
  $exp = $Opt['stageExpect'][$stage]
  return @{ classes = @($exp['absentClasses']); rel = @($exp['absentRel']) }
}

# =====================================================================================
if ($Mode -eq 'Capture') {
  if (-not $Stage) { Write-Output 'ABORT: -Stage required for Capture'; exit 2 }
  $dir = Get-ResolvedStage $Stage
  $nativeExe = Join-Path $dir.app 'YeManCC.exe'
  $hostDll   = Join-Path $dir.pc 'YeManInputHost.dll'
  $srcFiles = Get-SourceScopeFiles
  $rows = Get-StageArtifacts $dir
  $classDigests = @{}
  foreach ($k in $classRules.Keys) { $classDigests[$k] = Get-ClassDigest ([object[]]($rows | Where-Object { $_.cls -eq $k })) @() }
  $bad = [System.Collections.Generic.List[string]]::new()
  # shared strict checks (before any credential is written)
  foreach ($f in (Test-RequiredArtifacts $Stage $rows)) { $bad.Add($f) }
  $sdkLock = Get-SdkLockCheck (Join-Path $dir.pc ([string]$Opt['versionCompatibility']['sdkLockFile']))
  foreach ($f in (Test-SdkLockStrict $sdkLock $Stage)) { $bad.Add($f) }
  $probe = Resolve-ProbeTargets $nativeExe $hostDll
  $optOut = Get-OptStatus $probe.native $probe.host
  foreach ($o in $optOut) { if (-not $o.present) { $bad.Add('optimization not verifiable: ' + $o.name + ' (marker=' + $o.markerOk + ' anchor=' + $o.anchorOk + ')') } }
  $ea = Get-ExpectedAbsent $Stage
  $missing = [object[]]($rows | Where-Object { -not $_.sha256 })
  $unexpected = [object[]]($missing | Where-Object { ($ea.classes -notcontains $_.cls) -and ($ea.rel -notcontains ($_.cls + '|' + $_.rel)) })
  if ($unexpected.Count -gt 0) { $bad.Add('unexpected absent artifact(s): ' + (($unexpected | ForEach-Object { $_.cls + '/' + $_.rel }) -join ', ')) }
  $legacyHit = @()
  foreach ($s in @($Opt['artifacts']['legacySentinels'])) {
    $sc = $s; $hit = @($rows | Where-Object { $_.cls -eq $sc['class'] -and ($_.rel -replace '\\','/') -eq ($sc['rel'] -replace '\\','/') -and $_.sha256 -eq $sc['sha256'] })
    if ($hit.Count -gt 0) { $legacyHit += ($sc['class'] + '/' + $sc['rel']) }
  }
  if ($legacyHit.Count -gt 0) { $bad.Add('legacy sentinel present: ' + ($legacyHit -join ', ')) }
  $genRec = Test-GeneratedFiles $Stage $rows $bad
  $status = 'PASS'; if ($bad.Count -gt 0) { $status = 'FAIL' }
  $mf = [pscustomobject]@{
    schema = 2; mode = 'capture'
    baseline = $Opt['baselineName']; baselineSha256 = (Hash-File $cfgPath); rulesSha256 = Get-RulesDigest
    validationStatus = $status
    failures = [object[]]@($bad)
    stage = $Stage; capturedAt = (Get-Date).ToString('o'); machine = $env:COMPUTERNAME
    projectRoot = $ProjectRoot; workspaceRoot = $WorkspaceRoot
    adoptedOptimizations = $adopted
    optimizations = $optOut
    sourceFileCount = $srcFiles.Count; sourceTreeSha256 = Get-TreeDigest $srcFiles
    toolchain = Get-ToolchainInfo
    nativeExeSha256 = (Hash-File $nativeExe); hostDllSha256 = (Hash-File $hostDll)
    sdkLock = $sdkLock
    classDigests = $classDigests
    generatedFiles = $genRec
    legacySentinelsHit = $legacyHit
    artifacts = $rows
  }
  $outPath = $Out
  if (-not $outPath) { if ($Name) { $outPath = Join-Path $OutDir ($Name + '__' + $Stage + '.json') } else { $outPath = Join-Path $OutDir ('capture__' + $Stage + '.json') } }
  if ($bad.Count -gt 0) {
    $failPath = Get-FailurePath $outPath
    Write-JsonFile $failPath $mf   # failed credential is recorded separately, never clobbers the last good one
    Write-Output ('CAPTURED(FAILED) ' + $failPath)
    Write-Output ('CAPTURE GATE FAIL (' + $bad.Count + ')'); foreach ($b in $bad) { Write-Output ('  - ' + $b) }
    exit 6
  }
  Write-JsonAtomic $outPath $mf   # success written atomically over the previous good credential
  Write-Output ('CAPTURED ' + $outPath)
  Write-Output ('  validationStatus=PASS stage=' + $Stage + ' srcFiles=' + $mf.sourceFileCount + ' native=' + (Short-Hash $mf.nativeExeSha256) + ' host=' + (Short-Hash $mf.hostDllSha256))
  foreach ($o in $optOut) { Write-Output ('  OPT ' + $o.name + ' present=' + $o.present) }
  Write-Output 'CAPTURE_OK'
  exit 0
}

if ($Mode -eq 'Gate') {
  if ($Stage -notin @('package','release','install')) { Write-Output 'ABORT: Gate requires -Stage package|release|install'; exit 2 }
  if (-not $BuildManifest -or -not (Test-Path -LiteralPath $BuildManifest -PathType Leaf)) {
    Write-Output 'GATE FAIL: missing build manifest. A per-build capture is the authoritative record; refusing to gate against nothing.'; exit 3
  }
  $build = Read-Json $BuildManifest
  # The build capture must itself be a valid credential (shape + config identity).
  $bmFails = [System.Collections.Generic.List[string]]::new()
  foreach ($f in (Assert-ManifestShape $build $BuildManifest)) { $bmFails.Add($f) }
  foreach ($f in (Test-ConfigIdentity $build)) { $bmFails.Add($f) }
  if ($bmFails.Count -gt 0) {
    Write-Output ('GATE FAIL: build capture rejected as a credential (' + $bmFails.Count + '): ' + $BuildManifest)
    foreach ($f in $bmFails) { Write-Output ('  - ' + $f) }
    exit 4
  }
  $dir = Get-ResolvedStage $Stage
  $nativeExe = Join-Path $dir.app 'YeManCC.exe'
  $hostDll   = Join-Path $dir.pc 'YeManInputHost.dll'
  $rows = Get-StageArtifacts $dir
  $fails = [System.Collections.Generic.List[string]]::new()
  $probe = Resolve-ProbeTargets $nativeExe $hostDll
  $optOut = Get-OptStatus $probe.native $probe.host
  foreach ($o in $optOut) { if (-not $o.present) { $fails.Add('optimization not verifiable at ' + $Stage + ': ' + $o.name) } }
  $ea = Get-ExpectedAbsent $Stage
  $diskByKey = @{}
  foreach ($r in $rows) { $diskByKey[(Get-GenKey $r.cls $r.rel)] = $r }
  # per-artifact consistency vs the build manifest (must-match classes, generated excluded)
  foreach ($b in @($build.artifacts)) {
    if ($mustMatch -notcontains $b.cls) { continue }
    $key = Get-GenKey $b.cls $b.rel
    if ($genKeys -contains $key) { continue }
    if ($ea.classes -contains $b.cls -or $ea.rel -contains $key) { continue }   # legitimately absent here
    if (-not $b.sha256) { continue }   # legitimately absent at build time
    if (-not $diskByKey.ContainsKey($key)) {
      $fails.Add('artifact absent at ' + $Stage + ': ' + $key); continue
    }
    $now = $diskByKey[$key]
    if (-not $now.sha256) {
      $fails.Add('artifact absent at ' + $Stage + ': ' + $key); continue
    }
    if ($now.sha256 -ne $b.sha256) { $fails.Add('hash mismatch ' + $key + ' build=' + (Short-Hash $b.sha256) + ' ' + $Stage + '=' + (Short-Hash $now.sha256) + ' (old binary / tamper)') }
  }
  # extra must-match artifacts on disk not in build manifest
  $buildKeys = @{}
  foreach ($b in @($build.artifacts)) { if ($mustMatch -contains $b.cls) { $buildKeys[(Get-GenKey $b.cls $b.rel)] = $true } }
  foreach ($r in $rows) {
    if ($mustMatch -notcontains $r.cls) { continue }
    $key = Get-GenKey $r.cls $r.rel
    if ($genKeys -contains $key) { continue }
    if (-not $r.sha256) { continue }
    if (-not $buildKeys.ContainsKey($key)) { $fails.Add('unexpected extra artifact at ' + $Stage + ' (not in build manifest): ' + $key) }
  }
  # class digests vs build (excluding generated files + expected-absent classes)
  $classDigests = @{}
  foreach ($k in $classRules.Keys) {
    if ($ea.classes -contains $k) { $classDigests[$k] = '<expected-absent>'; continue }
    $classDigests[$k] = Get-ClassDigest ([object[]]($rows | Where-Object { $_.cls -eq $k })) $genKeys
    $expDigest = Get-ClassDigest ([object[]]($build.artifacts | Where-Object { $_.cls -eq $k })) $genKeys
    if ($classDigests[$k] -ne $expDigest) { $fails.Add('class digest mismatch at ' + $Stage + ': ' + $k + ' disk=' + (Short-Hash $classDigests[$k]) + ' build=' + (Short-Hash $expDigest)) }
  }
  $genRec = Test-GeneratedFiles $Stage $rows $fails
  # SDK lock: strict shared rule (missing constant / unparseable / missing core /
  # mismatch are all failures; expected-absent stages are exempted inside).
  $sdkLock = Get-SdkLockCheck (Join-Path $dir.pc ([string]$Opt['versionCompatibility']['sdkLockFile']))
  foreach ($f in (Test-SdkLockStrict $sdkLock $Stage)) { $fails.Add($f) }
  $missing = [object[]]($rows | Where-Object { -not $_.sha256 })
  $unexpectedAbsent = [object[]]($missing | Where-Object { ($ea.classes -notcontains $_.cls) -and ($ea.rel -notcontains ($_.cls + '|' + $_.rel)) })
  if ($unexpectedAbsent.Count -gt 0) { $fails.Add('unexpected absent at ' + $Stage + ': ' + (($unexpectedAbsent | ForEach-Object { $_.cls + '/' + $_.rel }) -join ', ')) }
  $legacyHit = @()
  foreach ($s in @($Opt['artifacts']['legacySentinels'])) {
    $sc = $s; $hit = @($rows | Where-Object { $_.cls -eq $sc['class'] -and ($_.rel -replace '\\','/') -eq ($sc['rel'] -replace '\\','/') -and $_.sha256 -eq $sc['sha256'] })
    if ($hit.Count -gt 0) { $legacyHit += ($sc['class'] + '/' + $sc['rel']) }
  }
  if ($legacyHit.Count -gt 0) { $fails.Add('legacy sentinel present: ' + ($legacyHit -join ', ')) }

  $report = [pscustomobject]@{
    schema = 2; mode = 'gate'; baseline = $Opt['baselineName']; baselineSha256 = (Hash-File $cfgPath)
    stage = $Stage; capturedAt = (Get-Date).ToString('o'); machine = $env:COMPUTERNAME
    buildManifest = $BuildManifest; buildTreeSha256 = $build.sourceTreeSha256
    nativeExeSha256 = (Hash-File $nativeExe); hostDllSha256 = (Hash-File $hostDll)
    optimizations = $optOut
    sdkLock = $sdkLock
    classDigests = $classDigests; generatedFiles = $genRec; legacySentinelsHit = $legacyHit
    failures = [object[]]$fails
  }
  $outPath = $Out
  if (-not $outPath) { $outPath = Join-Path $OutDir ('gate__' + $Stage + '.json') }
  Write-JsonFile $outPath $report
  Write-Output ('GATE_REPORT ' + $outPath)
  Write-Output ('  stage=' + $Stage + ' native=' + (Short-Hash $report.nativeExeSha256) + ' host=' + (Short-Hash $report.hostDllSha256) + ' build=' + (Short-Hash ([string]$build.sourceTreeSha256)))
  foreach ($o in $optOut) { Write-Output ('  OPT ' + $o.name + ' present=' + $o.present) }
  foreach ($g in $genRec) { Write-Output ('  gen ' + $g.rel + ' match=' + $g.matchesSource) }
  if ($fails.Count -gt 0) { Write-Output ('GATE FAIL (' + $fails.Count + ') -> block ' + $Stage); foreach ($f in $fails) { Write-Output ('  - ' + $f) }; exit 8 }
  Write-Output 'GATE_OK'
  exit 0
}

if ($Mode -eq 'Diff') {
  if (-not $Base -or -not $Cand) { Write-Output 'ABORT: -Base and -Cand required for Diff'; exit 2 }
  $b = Read-Json $Base; $c = Read-Json $Cand
  $fails = [System.Collections.Generic.List[string]]::new()
  $bSet = @($b.optimizations | ForEach-Object { $_.name })
  $cSet = @($c.optimizations | ForEach-Object { $_.name })
  $added = @($cSet | Where-Object { $bSet -notcontains $_ })
  $removed = @($bSet | Where-Object { $cSet -notcontains $_ })
  Write-Output ('DIFF ' + $b.stage + ' -> ' + $c.stage)
  Write-Output ('  optSet added=' + $(if ($added.Count) { $added -join ',' } else { '<none>' }) + ' removed=' + $(if ($removed.Count) { $removed -join ',' } else { '<none>' }))
  if ($ExpectAdded.Count -gt 0 -and ((@($ExpectAdded | Sort-Object) -join ',') -ne (@($added | Sort-Object) -join ','))) { $fails.Add('added set mismatch') }
  if ($removed.Count -gt 0) { $fails.Add('optimizations removed: ' + ($removed -join ',')) }
  foreach ($k in $mustMatch) {
    $same = ((Get-ClassDigest @($b.artifacts | Where-Object { $_.cls -eq $k }) @()) -eq (Get-ClassDigest @($c.artifacts | Where-Object { $_.cls -eq $k }) @()))
    Write-Output ('  class ' + $k.PadRight(18) + ' ' + $(if ($same) { 'SAME' } else { 'DIFFERENT' }))
    if (($ExpectSame -contains $k) -and -not $same) { $fails.Add('class must be identical across stages: ' + $k) }
    if (($ExpectChanged -contains $k) -and $same) { $fails.Add('class expected to change: ' + $k) }
  }
  if ($fails.Count -gt 0) { Write-Output ('DIFF FAIL (' + $fails.Count + ')'); foreach ($f in $fails) { Write-Output ('  - ' + $f) }; exit 7 }
  Write-Output 'DIFF_OK'; exit 0
}

if ($Mode -eq 'Verify') {
  if (-not $Manifest) { Write-Output 'ABORT: -Manifest required for Verify'; exit 2 }
  if (-not (Test-Path -LiteralPath $Manifest -PathType Leaf)) { Write-Output ('ABORT: manifest not found: ' + $Manifest); exit 2 }
  $m = Read-Json $Manifest
  # 1) manifest shape / type / stage / required fields, plus config identity bound
  #    to this build. A malformed, empty-artifact, FAILED or stale-rule capture is
  #    rejected outright and is never treated as a credential.
  $shape = [System.Collections.Generic.List[string]]::new()
  foreach ($f in (Assert-ManifestShape $m $Manifest)) { $shape.Add($f) }
  foreach ($f in (Test-ConfigIdentity $m)) { $shape.Add($f) }
  if ($shape.Count -gt 0) {
    Write-Output ('VERIFY REJECTED (manifest not a credential) ' + $Manifest)
    foreach ($f in $shape) { Write-Output ('  - ' + $f) }
    exit 4
  }
  $fails = [System.Collections.Generic.List[string]]::new()
  $dir = Get-ResolvedStage $m.stage
  $nativeExe = Join-Path $dir.app 'YeManCC.exe'; $hostDll = Join-Path $dir.pc 'YeManInputHost.dll'
  $rows = Get-StageArtifacts $dir
  # 3) required artifacts present (shared rule)
  foreach ($f in (Test-RequiredArtifacts $m.stage $rows)) { $fails.Add($f) }
  # 4) every recorded artifact must still match
  $byKey = @{}
  foreach ($r in $rows) { $byKey[(Get-GenKey $r.cls $r.rel)] = $r }
  foreach ($rec in @($m.artifacts)) {
    $key = Get-GenKey $rec.cls $rec.rel
    if (-not $byKey.ContainsKey($key)) { $fails.Add('artifact missing now: ' + $key); continue }
    $now = $byKey[$key]
    if ($now.sha256 -ne $rec.sha256) {
      $fails.Add('hash mismatch ' + $key + ' expected=' + (Short-Hash $rec.sha256) + ' now=' + (Short-Hash $now.sha256))
    }
  }
  # 5) optimization set (binary marker + source anchor)
  foreach ($o in (Get-OptStatus $nativeExe $hostDll)) { if (-not $o.present) { $fails.Add('optimization not verifiable now: ' + $o.name) } }
  # 6) strict SDK lock
  $sdkLock = Get-SdkLockCheck (Join-Path $dir.pc ([string]$Opt['versionCompatibility']['sdkLockFile']))
  foreach ($f in (Test-SdkLockStrict $sdkLock $m.stage)) { $fails.Add($f) }
  if ($fails.Count -gt 0) {
    Write-Output ('VERIFY FAIL (' + $fails.Count + ') ' + $Manifest)
    foreach ($f in $fails) { Write-Output ('  - ' + $f) }
    exit 6
  }
  Write-Output ('VERIFY OK ' + $Manifest + '  stage=' + $m.stage + ' artifacts=' + @($m.artifacts).Count)
  exit 0
}

# ---- MODE RELEASEPACKAGE (G4: release identity + hash + manifest + unzip cross-check) --
if ($Mode -eq 'ReleasePackage') {
  if (-not $Zip -or -not (Test-Path -LiteralPath $Zip -PathType Leaf)) { Write-Output ('ABORT: -Zip not found: ' + $Zip); exit 2 }
  if (-not $ZipRoot -or -not (Test-Path -LiteralPath $ZipRoot -PathType Container)) { Write-Output ('ABORT: -ZipRoot not found: ' + $ZipRoot); exit 2 }
  if ($IncludeRoots.Count -eq 0) { Write-Output 'ABORT: -IncludeRoots required (top-level dirs inside the archive)'; exit 2 }
  $fails = [System.Collections.Generic.List[string]]::new()
  $zipRootFull = [IO.Path]::GetFullPath($ZipRoot).TrimEnd('\')
  # expected file set from the real export directories
  $expected = @{}
  $manifestRows = [System.Collections.Generic.List[object]]::new()
  foreach ($root in $IncludeRoots) {
    $dir = Join-Path $zipRootFull $root
    if (-not (Test-Path -LiteralPath $dir -PathType Container)) { $fails.Add('export root missing: ' + $root); continue }
    foreach ($f in (Get-ChildItem -LiteralPath $dir -Recurse -File -Force)) {
      $rel = $f.FullName.Substring($zipRootFull.Length + 1).Replace('\', '/')
      $h = Hash-File $f.FullName
      $expected[$rel] = $h
      $manifestRows.Add([pscustomobject]@{ entry = $rel; size = $f.Length; sha256 = $h })
    }
  }
  $manifestSorted = Sort-Ordinal @($manifestRows.ToArray()) { param($x) [string]$x.entry }
  $manifestRows = [System.Collections.Generic.List[object]]::new()
  foreach ($ms in $manifestSorted) { $manifestRows.Add($ms) }
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $zipHash = Hash-File $Zip
  $zipLen = (Get-Item -LiteralPath $Zip).Length
  $archive = [IO.Compression.ZipFile]::OpenRead($Zip)
  $zipEntries = @{}
  try {
    foreach ($e in $archive.Entries) {
      $name = $e.FullName.Replace('\', '/').TrimStart('/')
      if (-not $name) { continue }
      if ($name.EndsWith('/')) { continue }   # directory entry
      $sha = [System.Security.Cryptography.SHA256]::Create()
      try { $s = $e.Open(); try { $eh = ([BitConverter]::ToString($sha.ComputeHash($s))).Replace('-', '') } finally { $s.Dispose() } } finally { $sha.Dispose() }
      $zipEntries[$name] = [pscustomobject]@{ size = $e.Length; sha256 = $eh }
    }
  } finally { $archive.Dispose() }
  foreach ($k in $expected.Keys) {
    if (-not $zipEntries.ContainsKey($k)) { $fails.Add('archive entry missing: ' + $k); continue }
    if ($zipEntries[$k].sha256 -ne $expected[$k]) { $fails.Add('archive entry hash mismatch: ' + $k + ' zip=' + (Short-Hash $zipEntries[$k].sha256) + ' dir=' + (Short-Hash $expected[$k])) }
  }
  foreach ($k in $zipEntries.Keys) { if (-not $expected.ContainsKey($k)) { $fails.Add('archive has extra entry not in export dir: ' + $k) } }
  # Bind this archive to THIS run's build capture: the build capture must be a
  # valid credential, and the archive's native exe must equal its recorded hash
  # (a previously generated identity is never accepted as a new package's proof).
  $bmNative = ''; $bmBound = $false
  if ($BuildManifest) {
    if (-not (Test-Path -LiteralPath $BuildManifest -PathType Leaf)) { Write-Output ('ABORT: -BuildManifest not found: ' + $BuildManifest); exit 3 }
    $bm = Read-Json $BuildManifest
    $bmF = [System.Collections.Generic.List[string]]::new()
    foreach ($f in (Assert-ManifestShape $bm $BuildManifest)) { $bmF.Add($f) }
    foreach ($f in (Test-ConfigIdentity $bm)) { $bmF.Add($f) }
    if ($bmF.Count -gt 0) {
      Write-Output ('RELEASE PACKAGE ABORT: build capture rejected (' + $bmF.Count + ')')
      foreach ($f in $bmF) { Write-Output ('  - ' + $f) }
      exit 4
    }
    foreach ($a in @($bm.artifacts)) { if ($a.cls -eq 'native-exe' -and ($a.rel -replace '\\','/') -eq 'YeManCC.exe') { $bmNative = [string]$a.sha256 } }
    $bmBound = $true
    if (-not $bmNative) { $fails.Add('build capture has no native-exe|YeManCC.exe hash to bind against') }
  }
  # resolve the archive's own YeManCC.exe entry (root order is not guaranteed)
  $exeKey = ''; $exeHash = ''
  foreach ($root in $IncludeRoots) {
    $k = $root + '/YeManCC.exe'
    if ($zipEntries.ContainsKey($k)) { $exeKey = $k; $exeHash = $zipEntries[$k].sha256; break }
  }
  if (-not $exeKey) { $fails.Add('archive contains no <root>/YeManCC.exe') }
  elseif ($bmBound -and $bmNative -and $exeHash -ne $bmNative) {
    $fails.Add('archive native exe != build capture: zip=' + (Short-Hash $exeHash) + ' build=' + (Short-Hash $bmNative) + ' (stale or foreign package)')
  }
  $primaryExe = ''; if ($exeKey) { $primaryExe = Join-Path $zipRootFull ($exeKey -replace '/','\') }
  $identity = [pscustomobject]@{
    schema = 2; mode = 'release-package'; baseline = $Opt['baselineName']
    baselineSha256 = (Hash-File $cfgPath); rulesSha256 = Get-RulesDigest
    capturedAt = (Get-Date).ToString('o'); machine = $env:COMPUTERNAME
    validationStatus = $(if ($fails.Count -eq 0) { 'PASS' } else { 'FAIL' })
    zip = $Zip; zipSha256 = $zipHash; zipBytes = $zipLen
    zipEntryCount = $zipEntries.Count; exportFileCount = $expected.Count
    zipRoot = $zipRootFull; includeRoots = $IncludeRoots
    releaseExe = $primaryExe; releaseExeSha256 = $exeHash
    buildManifest = $BuildManifest; buildManifestSha256 = $(if ($bmBound) { Hash-File $BuildManifest } else { '' })
    buildBoundExeSha256 = $bmNative
    unzipCrossCheck = 'per-entry sha256 of every archive file == on-disk export file'
    manifestEntryCount = $manifestRows.Count
    entries = [object[]]$manifestRows
    failures = [object[]]$fails
  }
  # identity is keyed by the package hash, so an identity can never be reused as
  # evidence for a different package; a failed run never overwrites a good one.
  $outPath = $Out
  if (-not $outPath) {
    $key = [IO.Path]::GetFileNameWithoutExtension($Zip) + '.' + $zipHash.Substring(0, 8)
    $outPath = Join-Path $OutDir ($key + '__release-identity.json')
  }
  if ($fails.Count -gt 0) {
    $failPath = Get-FailurePath $outPath
    Write-JsonFile $failPath $identity
    Write-Output ('RELEASE_IDENTITY(FAILED) ' + $failPath)
    Write-Output ('  zip=' + $Zip)
    Write-Output ('  zipSha256=' + $zipHash)
    Write-Output ('RELEASE PACKAGE CROSS-CHECK FAIL (' + $fails.Count + ')')
    foreach ($f in $fails) { Write-Output ('  - ' + $f) }
    exit 8
  }
  Write-JsonAtomic $outPath $identity
  Write-Output ('RELEASE_IDENTITY ' + $outPath)
  Write-Output ('  zip=' + $Zip)
  Write-Output ('  zipSha256=' + $zipHash)
  Write-Output ('  zipBytes=' + $zipLen + ' zipEntries=' + $zipEntries.Count + ' exportFiles=' + $expected.Count)
  Write-Output ('  releaseExe=' + $exeHash + ' buildBound=' + $bmBound + ' buildExe=' + (Short-Hash $bmNative))
  Write-Output 'RELEASE_PACKAGE_OK (unzip cross-check passed)'
  exit 0
}

Write-Output 'ABORT: unknown mode'; exit 2