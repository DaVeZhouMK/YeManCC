#requires -Version 5.1
<#
  FAN-926R (2026-09-26): controlled, tool-driven Fan Host payload rebaseline / re-pin.

  Scope (adjudication FAN-926R section 1.1):
    1. freeze inputs + backup preimages (written OUTSIDE the payload manifest)
    2. build the actual Host artifacts via the formal tools/build-fan-host-payload.ps1
    3. update YeManFanHost.authorization.md approved values from the ACTUAL artifacts
       (history preserved, this basis appended)
    4. regenerate the payload manifest after the authorization is final
       (the manifest lists authorization.md, so the order is mandatory)
    5. re-pin the manifest consumers (native/main.cpp + verify-r5v9) from the
       recomputed manifest SHA256
    6. apply + read back, stop on conflict
    7. do NOT claim cross-file atomicity

  Safety:
    - -DryRun prints the full plan and writes NOTHING (no build, no evidence dir).
    - Nothing is hand-typed: every hash is computed from approved sources/artifacts.
    - Preimage guards: the current on-disk values must equal the recorded preimage,
      otherwise the tool refuses to apply (drift/conflict).
    - Apply writes to a sibling temp file and moves it into place, then reads back.
    - On failure only files whose content still equals THIS tool's postimage are
      reverted; any other concurrent edit is preserved and reported as a conflict.

  This file is intentionally pure ASCII: PowerShell 5.1 misdecodes BOM-less
  non-ASCII scripts and drops the following statement. Run under Windows
  PowerShell 5.1 - the payload manifest byte format (UTF-8 BOM, 4-space indent,
  two spaces after the colon) is what Set-Content -Encoding UTF8 emits there and
  is what the frozen generations already carry.
#>
[CmdletBinding()]
param(
  [switch]$DryRun,
  [string]$ProjectRoot = '',
  [string]$HcRuntimeSourceRoot = '',
  [string]$EvidenceRoot = '',
  # Optional preimage assertions. When supplied they must equal the actual
  # on-disk values or the tool refuses to run (negative-control support).
  [string]$ExpectedOldManifestSha256 = '',
  [string]$ExpectedOldHostDllSha256 = ''
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------- helpers
function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = [IO.File]::OpenRead($Path)
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose() }
  } finally { $sha.Dispose() }
}

function Get-Sha256Bytes([byte[]]$Bytes) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant() }
  finally { $sha.Dispose() }
}

function Get-Rel([string]$Root, [string]$Path) {
  $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
  return ([IO.Path]::GetFullPath($Path)).Substring($rootFull.Length).TrimStart('\')
}

function Get-HasBom([byte[]]$Bytes) {
  return ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF)
}

function Get-TextBytes([string]$Text, [bool]$WithBom) {
  $body = (New-Object Text.UTF8Encoding($false)).GetBytes($Text)
  if (-not $WithBom) { return $body }
  $preamble = (New-Object Text.UTF8Encoding($true)).GetPreamble()
  $out = New-Object byte[] ($preamble.Length + $body.Length)
  [Array]::Copy($preamble, 0, $out, 0, $preamble.Length)
  [Array]::Copy($body, 0, $out, $preamble.Length, $body.Length)
  return $out
}

# Replace only capture group 1 of every match; assert the hit count and return
# the captured (preimage) values so the caller can compare against the record.
function Set-Pin([string]$Text, [string]$Pattern, [string]$NewValue, [int]$ExpectedCount, [string]$Label) {
  $matches = [regex]::Matches($Text, $Pattern)
  if ($matches.Count -ne $ExpectedCount) {
    throw "rebaseline pin '$Label': expected $ExpectedCount match(es), found $($matches.Count)"
  }
  $result = $Text
  $olds = @()
  $lines = @()
  for ($i = $matches.Count - 1; $i -ge 0; $i--) {
    $m = $matches[$i]
    $g = $m.Groups[1]
    $olds = @($g.Value) + $olds
    $lines = @((($Text.Substring(0, $m.Index) -split "`n").Count)) + $lines
    $result = $result.Substring(0, $g.Index) + $NewValue + $result.Substring($g.Index + $g.Length)
  }
  return [ordered]@{ Text = $result; Olds = $olds; Lines = $lines; Count = $matches.Count }
}

# New update layouts bind the incoming runtime manifest instead of a fixed HC hash.
# Preserve that validation verbatim; unknown/partial layouts still stop the rebaseline.
function Set-NativeHcPin([string]$Text, [string]$HcHash) {
  $legacy = '\(\[string\]\$hc\[0\]\.sha256\) -ine ''([0-9A-Fa-f]{64})'''
  if ([regex]::Matches($Text, $legacy).Count -gt 0) {
    $plan = Set-Pin $Text $legacy $HcHash 1 'native.main.hcPin'
    $plan.NewAudit = $HcHash
    return $plan
  }
  $guards = @(
    '(Get-FileHash -LiteralPath $runtimeManifestPath -Algorithm SHA256).Hash -ine ([string]$binding.runtimeManifestSha256)',
    '(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine ([string]$entry.sha256)',
    '$hc.Count -ne 1 -or ([string]$hc[0].sha256) -notmatch ''^[0-9A-Fa-f]{64}$'''
  )
  $start = $Text.IndexOf('function Assert-FanHostV2Payload', [StringComparison]::Ordinal)
  $end = if ($start -ge 0) { $Text.IndexOf('function Normalize-ManifestRelativePath', $start, [StringComparison]::Ordinal) } else { -1 }
  if ($start -lt 0 -or $end -le $start) { throw 'rebaseline native HC validation boundary not found' }
  $runtimeStart = $Text.IndexOf('$runtimeManifestPath = Join-Path $fanHostRuntimeSource', $start, [StringComparison]::Ordinal)
  if ($runtimeStart -lt $start -or $runtimeStart -ge $end) { throw 'rebaseline native HC runtime boundary not found' }
  $validation = $Text.Substring($runtimeStart, $end - $runtimeStart)
  foreach ($guard in $guards) {
    if ([regex]::Matches($validation, [regex]::Escape($guard)).Count -ne 1) {
      throw 'rebaseline native HC identity: expected unique incoming-manifest/hash guards'
    }
  }
  $index = $Text.IndexOf($guards[2], [StringComparison]::Ordinal)
  return [ordered]@{ Text = $Text; Olds = @('incoming-runtime-manifest');
    Lines = @(($Text.Substring(0, $index) -split "`n").Count); Count = 1;
    NewAudit = 'incoming-runtime-manifest (unchanged)' }
}

function Write-Utf8Atomic([string]$Path, [byte[]]$Bytes) {
  $tmp = Join-Path (Split-Path -Parent $Path) ('.rebaseline-' + [guid]::NewGuid().ToString('N') + '.tmp')
  try {
    [IO.File]::WriteAllBytes($tmp, $Bytes)
    if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Force }
    Move-Item -LiteralPath $tmp -Destination $Path
  } catch {
    # Never leave a stray temp file behind on failure.
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
    throw
  }
  $back = [IO.File]::ReadAllBytes($Path)
  if ((Get-Sha256Bytes $back) -ne (Get-Sha256Bytes $Bytes)) { throw "readback mismatch after writing: $Path" }
}

# ---------------------------------------------------------------- resolve paths
$project = if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
  [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
} else { [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\') }

$payloadRoot = Join-Path $project 'PowerControl\fan-host'
$nativeMain = Join-Path $project 'native\main.cpp'
$verifier = Join-Path $project 'tools\verify-r5v9-fan-host-payload.ps1'
$authPath = Join-Path $payloadRoot 'YeManFanHost.authorization.md'
$manifestPath = Join-Path $payloadRoot 'YeManFanHost.payload.json'
$payloadDll = Join-Path $payloadRoot 'YeManFanHost.dll'
$payloadExe = Join-Path $payloadRoot 'YeManFanHost.exe'
$installPs1 = Join-Path $payloadRoot 'install-fan-host-payload.ps1'
$buildScript = Join-Path $project 'tools\build-fan-host-payload.ps1'
$realHostProgram = Join-Path $project 'FanLab\real-host\Program.cs'
$lightSetterProgram = Join-Path $project 'FanLab\LightSetter\Program.cs'
$hostOutDir = Join-Path $project 'FanLab\real-host\bin\Release\net10.0-windows10.0.19041.0\win-x64'

foreach ($required in @($nativeMain, $verifier, $authPath, $manifestPath, $payloadDll, $payloadExe, $buildScript, $realHostProgram, $lightSetterProgram)) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "rebaseline input missing: $required" }
}

if ([string]::IsNullOrWhiteSpace($HcRuntimeSourceRoot)) {
  $authForId = Get-Content -LiteralPath $authPath -Raw -Encoding UTF8
  $baselineId = [regex]::Match($authForId, '(?m)^baselineId:\s*([^\r\n]+)').Groups[1].Value.Trim()
  if ([string]::IsNullOrWhiteSpace($baselineId)) { throw 'rebaseline: authorization baselineId missing' }
  $hcRoot = Join-Path $project ("PowerControl\handheldcompanion-runtime\$baselineId")
} else {
  $hcRoot = [IO.Path]::GetFullPath($HcRuntimeSourceRoot).TrimEnd('\')
}
$hcDllPath = Join-Path $hcRoot 'HandheldCompanion.dll'
$hcRuntimeManifest = Join-Path $hcRoot 'HandheldCompanion.runtime.json'
if (-not (Test-Path -LiteralPath $hcRuntimeManifest -PathType Leaf)) { throw "rebaseline: HC runtime manifest missing: $hcRuntimeManifest" }
if (-not (Test-Path -LiteralPath $hcDllPath -PathType Leaf)) { throw "rebaseline: HC runtime DLL missing: $hcDllPath" }
if (-not (Test-Path -LiteralPath $installPs1 -PathType Leaf)) { throw "rebaseline: install script missing: $installPs1" }

# ---------------------------------------------------------------- step 1: preimage
$pre = [ordered]@{}
$pre.manifestSha256 = Get-Sha256 $manifestPath
$pre.hostDllSha256 = Get-Sha256 $payloadDll
$pre.hostExeSha256 = Get-Sha256 $payloadExe
$pre.hcDllSha256 = Get-Sha256 $hcDllPath
$pre.installPs1Sha256 = Get-Sha256 $installPs1
$pre.nativeBytes = [IO.File]::ReadAllBytes($nativeMain)
$pre.verifyBytes = [IO.File]::ReadAllBytes($verifier)
$pre.authBytes = [IO.File]::ReadAllBytes($authPath)
$pre.manifestBytes = [IO.File]::ReadAllBytes($manifestPath)
$pre.nativeText = [IO.File]::ReadAllText($nativeMain)
$pre.verifyText = [IO.File]::ReadAllText($verifier)
$pre.authText = [IO.File]::ReadAllText($authPath)
$pre.nativeHasBom = Get-HasBom $pre.nativeBytes
$pre.verifyHasBom = Get-HasBom $pre.verifyBytes
$pre.authHasBom = Get-HasBom $pre.authBytes
$pre.realHostBytes = [IO.File]::ReadAllBytes($realHostProgram)
$pre.lightSetterBytes = [IO.File]::ReadAllBytes($lightSetterProgram)
$pre.realHostText = [IO.File]::ReadAllText($realHostProgram)
$pre.lightSetterText = [IO.File]::ReadAllText($lightSetterProgram)
$pre.realHostHasBom = Get-HasBom $pre.realHostBytes
$pre.lightSetterHasBom = Get-HasBom $pre.lightSetterBytes

if (-not [string]::IsNullOrWhiteSpace($ExpectedOldManifestSha256) -and
    $ExpectedOldManifestSha256.ToLowerInvariant() -ne $pre.manifestSha256) {
  throw "rebaseline preimage mismatch (manifest): expected $ExpectedOldManifestSha256, actual $($pre.manifestSha256)"
}
if (-not [string]::IsNullOrWhiteSpace($ExpectedOldHostDllSha256) -and
    $ExpectedOldHostDllSha256.ToLowerInvariant() -ne $pre.hostDllSha256) {
  throw "rebaseline preimage mismatch (host dll): expected $ExpectedOldHostDllSha256, actual $($pre.hostDllSha256)"
}

# ---------------------------------------------------------------- step 2: HC identity source pins + build
# The HC identity is carried by more than the payload manifest: the Host and
# LightSetter sources pin ExpectedHcSha256, and the Host pin is COMPILED INTO
# YeManFanHost.dll. Propagate the new HC identity into those sources BEFORE the
# build, otherwise the rebuilt payload Host would still embed the superseded
# closure identity. Every value is computed from $pre.hcDllSha256 (the staged
# runtime), never hand-typed.
$newHc = $pre.hcDllSha256
$hcUpper = $newHc.ToUpperInvariant()
$hcShort = $newHc.Substring(0, 8)

$realHostPin = Set-Pin $pre.realHostText 'ExpectedHcSha256\s*=\s*"([0-9a-f]{64})"' $newHc 1 'realHost.ExpectedHcSha256'
$realHostText2 = $realHostPin.Text
$realHostComment = Set-Pin $realHostText2 'HandheldCompanion\.dll[\s\S]{0,4}?([0-9A-Fa-f]{8})\u2026' $hcShort 1 'realHost.hcComment'
$realHostText2 = $realHostComment.Text
$newRealHostBytes = Get-TextBytes $realHostText2 $pre.realHostHasBom

$lightSetterPin = Set-Pin $pre.lightSetterText 'ExpectedHcSha256\s*=\s*"([0-9a-f]{64})"' $newHc 1 'lightSetter.ExpectedHcSha256'
$lightSetterText2 = $lightSetterPin.Text
$lightSetterComment = Set-Pin $lightSetterText2 '(?m)^[ \t]*//[^\r\n]*\u83B7\u51C6\u5019\u9009[ \t]+([0-9A-Fa-f]{8})\u2026' $hcShort 1 'lightSetter.hcComment'
$lightSetterText2 = $lightSetterComment.Text
$newLightSetterBytes = Get-TextBytes $lightSetterText2 $pre.lightSetterHasBom

$sourceTargets = @(
  [ordered]@{ Name = 'FanLab\real-host\Program.cs'; Path = $realHostProgram; PreHash = (Get-Sha256Bytes $pre.realHostBytes); PreBytes = $pre.realHostBytes; NewBytes = $newRealHostBytes },
  [ordered]@{ Name = 'FanLab\LightSetter\Program.cs'; Path = $lightSetterProgram; PreHash = (Get-Sha256Bytes $pre.lightSetterBytes); PreBytes = $pre.lightSetterBytes; NewBytes = $newLightSetterBytes }
)
foreach ($t in $sourceTargets) { $t.NewHash = Get-Sha256Bytes $t.NewBytes; $t.CurrentHash = Get-Sha256 $t.Path }

$buildLog = @()
$preWritten = @()
if ($DryRun) {
  # Preview only: use the artifacts already produced by the prior Host compile.
  if (-not (Test-Path -LiteralPath (Join-Path $hostOutDir 'YeManFanHost.dll') -PathType Leaf)) {
    throw "rebaseline -DryRun: Host build output missing: $hostOutDir"
  }
  $newDll = Get-Sha256 (Join-Path $hostOutDir 'YeManFanHost.dll')
  $newExe = Get-Sha256 (Join-Path $hostOutDir 'YeManFanHost.exe')
} else {
  # The Host pin must be on disk at compile time, so write the source pins first.
  foreach ($t in $sourceTargets) {
    if ($t.CurrentHash -ne $t.NewHash) { Write-Utf8Atomic $t.Path $t.NewBytes; $preWritten += $t }
  }
  $previous = $env:YEMAN_ALLOW_R5V9_HOST_REBUILD
  try {
    $env:YEMAN_ALLOW_R5V9_HOST_REBUILD = '1'
    $buildLog = & $buildScript -HcRuntimeSourceRoot $hcRoot 2>&1
    if ($LASTEXITCODE -ne 0) { throw "rebaseline: build-fan-host-payload.ps1 failed: exit=$LASTEXITCODE" }
  } catch {
    $buildError = $_
    # Nothing else has been written yet, so a failed build is a clean revert.
    foreach ($t in $preWritten) { [IO.File]::WriteAllBytes($t.Path, $t.PreBytes) }
    throw ("rebaseline: build failed, source pins reverted: " + $buildError.Exception.Message)
  } finally {
    if ($null -eq $previous) { Remove-Item Env:\YEMAN_ALLOW_R5V9_HOST_REBUILD -ErrorAction SilentlyContinue }
    else { $env:YEMAN_ALLOW_R5V9_HOST_REBUILD = $previous }
  }
  $newDll = Get-Sha256 $payloadDll
  $newExe = Get-Sha256 $payloadExe
}

# ---------------------------------------------------------------- step 3: authorization
$newAuthText = $pre.authText
$authDll = Set-Pin $newAuthText 'approvedHostDllSha256:\s*([0-9A-Fa-f]{64})' $newDll.ToUpperInvariant() 1 'auth.approvedHostDllSha256'
$newAuthText = $authDll.Text
$authExe = Set-Pin $newAuthText 'approvedHostExeSha256:\s*([0-9A-Fa-f]{64})' $newExe.ToUpperInvariant() 1 'auth.approvedHostExeSha256'
$newAuthText = $authExe.Text
$authHc = Set-Pin $newAuthText 'approvedHcSha256:\s*([0-9A-Fa-f]{64})' $newHc.ToUpperInvariant() 1 'auth.approvedHcSha256'
$newAuthText = $authHc.Text

$basisLine = 'fan936R4MsiWireContractRepin: 2026-10-02 FAN-936-R4 MSI fan-table wire contract targeted-fix re-pin via tools/rebaseline-fan-host-payload.ps1 - the HC runtime closure HandheldCompanion.dll was REBUILT and REPLACED (58aebb01... superseded by 5f3eea17...) so the MSI family accepts the REAL 31-byte fan-table payload form returned by the EC after the single status/flag byte is stripped: the R2/R3 candidate wrongly required Data.Length >= 32 before any write, but the actual fan data is 31 bytes (the written packet is 32 bytes total with a 1-byte block index), so on a real 258V every write was refused as read truncated 31/32 and the enable never took effect; the fix keeps the unknown-tail protection (only payload bytes 1..6 - the six duty values - are rewritten, and payload[0], payload[7], payload[8..30] are carried through byte-for-byte) while a genuinely short read (under 31), a flag failure, or a read-back mismatch is still treated as a failure instead of a reported success; the CTW short-table zero-fill and its continue-after-failure behavior are NOT adopted; NOT reused byte-for-byte, so approvedHcSha256 now equals the staged runtime DLL; approvedHostDllSha256/approvedHostExeSha256 recomputed from the rebuilt Host and the Host ExpectedHcSha256 pin (FanLab/real-host/Program.cs), the native/main.cpp HC pin and the tools/verify-r5v9-fan-host-payload.ps1 HC marker were re-anchored to the same identity; payload manifest regenerated after this authorization update and the manifest pins re-anchored to the recomputed manifest'
if ($newAuthText -notmatch '(?m)^fan936R4MsiWireContractRepin:') {
  $fenceOpen = $newAuthText.IndexOf('```')
  if ($fenceOpen -lt 0) { throw 'rebaseline: authorization code fence not found' }
  $fenceClose = $newAuthText.IndexOf('```', $fenceOpen + 3)
  if ($fenceClose -lt 0) { throw 'rebaseline: authorization code fence not closed' }
  $newAuthText = $newAuthText.Substring(0, $fenceClose) + $basisLine + "`n" + $newAuthText.Substring($fenceClose)
}
$newAuthBytes = Get-TextBytes $newAuthText $pre.authHasBom

# ---------------------------------------------------------------- step 4: manifest
$hcMeta = Get-Content -LiteralPath $hcRuntimeManifest -Raw -Encoding UTF8 | ConvertFrom-Json
$runtimeId = [string]$hcMeta.runtimeId
if ($hcMeta.schemaVersion -ne 1 -or [string]::IsNullOrWhiteSpace($runtimeId)) { throw 'rebaseline: HC runtime identity invalid' }
$runtimeManifestRel = "..\handheldcompanion-runtime\$([IO.Path]::GetFileName($hcRoot))\HandheldCompanion.runtime.json"

$effective = @{
  'YeManFanHost.dll' = $newDll
  'YeManFanHost.exe' = $newExe
  'YeManFanHost.authorization.md' = Get-Sha256Bytes $newAuthBytes
}
$manifestItems = @(Get-ChildItem -LiteralPath $payloadRoot -File -Recurse | Where-Object {
    $_.Name -notin @('YeManFanHost.payload.json', 'YeManFanHost.session') -and
    $_.DirectoryName -notmatch '\\logs(?:\\|$)'
  } | Sort-Object FullName | ForEach-Object {
    $rel = (Get-Rel $payloadRoot $_.FullName).Replace('\', '/')
    $sha = if ($effective.ContainsKey($rel)) { $effective[$rel] } else { Get-Sha256 $_.FullName }
    [ordered]@{ path = $rel; sha256 = $sha }
  })
if ($manifestItems.Count -eq 0) { throw 'rebaseline: payload manifest would be empty' }
$manifestObj = [ordered]@{ schemaVersion = 2; runtimeManifest = $runtimeManifestRel; runtimeId = $runtimeId; files = $manifestItems }
$manifestJson = $manifestObj | ConvertTo-Json -Depth 4
$newManifestBytes = Get-TextBytes ($manifestJson + "`r`n") $true
$newManifestSha256 = Get-Sha256Bytes $newManifestBytes
$newFileCount = $manifestItems.Count

# ---------------------------------------------------------------- step 5: re-pin consumers
$nativePlan = Set-Pin $pre.nativeText '\$expectedFanHostV2ManifestSha256\s*=\s*''([0-9A-Fa-f]{64})''' $newManifestSha256 1 'native.main.manifest'
$nativeText2 = $nativePlan.Text
$nativeCount = Set-Pin $nativeText2 '\$expectedFanHostV2ManifestFileCount\s*=\s*(\d+)' ([string]$newFileCount) 1 'native.main.fileCount'
$nativeText2 = $nativeCount.Text
$nativeHc = Set-NativeHcPin $nativeText2 $hcUpper
$nativeText2 = $nativeHc.Text
$newNativeBytes = Get-TextBytes $nativeText2 $pre.nativeHasBom

$verifyPlan = Set-Pin $pre.verifyText '\$expectedManifestSha256\s*=\s*''([0-9A-Fa-f]{64})''' $newManifestSha256 1 'verify.manifest'
$verifyText2 = $verifyPlan.Text
$verifyDll = Set-Pin $verifyText2 "'YeManFanHost\.dll'\s*=\s*'([0-9A-Fa-f]{64})'" $newDll.ToUpperInvariant() 1 'verify.dll'
$verifyText2 = $verifyDll.Text
$verifyDllMarker = Set-Pin $verifyText2 'approvedHostDllSha256:\s*([0-9A-Fa-f]{64})' $newDll.ToUpperInvariant() 1 'verify.dllMarker'
$verifyText2 = $verifyDllMarker.Text
$verifyExe = Set-Pin $verifyText2 "'YeManFanHost\.exe'\s*=\s*'([0-9A-Fa-f]{64})'" $newExe.ToUpperInvariant() 1 'verify.exe'
$verifyText2 = $verifyExe.Text
$verifyExeMarker = Set-Pin $verifyText2 'approvedHostExeSha256:\s*([0-9A-Fa-f]{64})' $newExe.ToUpperInvariant() 1 'verify.exeMarker'
$verifyText2 = $verifyExeMarker.Text
$verifyHcMarker = Set-Pin $verifyText2 'approvedHcSha256:\s*([0-9A-Fa-f]{64})' $hcUpper 1 'verify.hcMarker'
$verifyText2 = $verifyHcMarker.Text
$verifyHcComment = Set-Pin $verifyText2 'HandheldCompanion\.dll \(([0-9A-Fa-f]{8})\.\.\.\)' $hcShort 1 'verify.hcComment'
$verifyText2 = $verifyHcComment.Text
$newVerifyBytes = Get-TextBytes $verifyText2 $pre.verifyHasBom

# The formal builder legitimately rewrites the manifest during step 2, so the
# manifest's rollback preimage is its post-build content (we own it here), not
# the original preimage. The manifest is never treated as an external conflict.
$manifestPreBytes = [IO.File]::ReadAllBytes($manifestPath)
$manifestPreHash = Get-Sha256Bytes $manifestPreBytes

# Source pins come first: in APPLY they were already written before the build
# (step 2), so they normally resolve to state 'current' here and are not
# rewritten. In DRY-RUN they still show as pending so the plan is reviewable.
$targets = @() + $sourceTargets + @(
  [ordered]@{ Name = 'native\main.cpp'; Path = $nativeMain; PreHash = (Get-Sha256Bytes $pre.nativeBytes); PreBytes = $pre.nativeBytes; NewBytes = $newNativeBytes },
  [ordered]@{ Name = 'tools\verify-r5v9-fan-host-payload.ps1'; Path = $verifier; PreHash = (Get-Sha256Bytes $pre.verifyBytes); PreBytes = $pre.verifyBytes; NewBytes = $newVerifyBytes },
  [ordered]@{ Name = 'PowerControl\fan-host\YeManFanHost.authorization.md'; Path = $authPath; PreHash = (Get-Sha256Bytes $pre.authBytes); PreBytes = $pre.authBytes; NewBytes = $newAuthBytes },
  [ordered]@{ Name = 'PowerControl\fan-host\YeManFanHost.payload.json'; Path = $manifestPath; PreHash = $manifestPreHash; PreBytes = $manifestPreBytes; NewBytes = $newManifestBytes; Owned = $true }
)
foreach ($t in $targets) { $t.NewHash = Get-Sha256Bytes $t.NewBytes; $t.CurrentHash = Get-Sha256 $t.Path }

# ---------------------------------------------------------------- plan output
Write-Output '==== FAN-926R FAN HOST PAYLOAD REBASELINE PLAN ===='
Write-Output ("ProjectRoot : $project")
Write-Output ("HcRuntime   : $hcRoot")
Write-Output ("Mode        : " + $(if ($DryRun) { 'DRY-RUN (no writes)' } else { 'APPLY' }))
Write-Output ''
Write-Output 'Artifacts:'
Write-Output ("  host dll   old=$($pre.hostDllSha256)  new=$newDll")
Write-Output ("  host exe   old=$($pre.hostExeSha256)  new=$newExe")
Write-Output ("  hc dll     staged=$newHc (payload runtime; supersedes the authorization pin)")
Write-Output ("  manifest   old=$($pre.manifestSha256)  new=$newManifestSha256")
Write-Output ("  file count old/now=$newFileCount")
if ($DryRun) {
  Write-Output ("  source pins (Host/LightSetter) computed; NOT written in DRY-RUN; the Host pin is compiled into YeManFanHost.dll at APPLY")
} else {
  Write-Output ("  source pins written pre-build: " + (($sourceTargets | ForEach-Object { $_.Name }) -join ', '))
}
Write-Output ''
Write-Output 'Symbols (file:line : old -> new : hits):'
$sym = @()
$sym += [pscustomobject]@{ Sym = 'native $expectedFanHostV2ManifestSha256'; File = 'native\main.cpp'; Line = $nativePlan.Lines[0]; Old = $nativePlan.Olds[0]; New = $newManifestSha256; Hits = 1 }
$sym += [pscustomobject]@{ Sym = 'native $expectedFanHostV2ManifestFileCount'; File = 'native\main.cpp'; Line = $nativeCount.Lines[0]; Old = $nativeCount.Olds[0]; New = [string]$newFileCount; Hits = 1 }
$sym += [pscustomobject]@{ Sym = 'verify $expectedManifestSha256'; File = 'tools\verify-r5v9-fan-host-payload.ps1'; Line = $verifyPlan.Lines[0]; Old = $verifyPlan.Olds[0]; New = $newManifestSha256; Hits = 1 }
$sym += [pscustomobject]@{ Sym = "verify 'YeManFanHost.dll' pin"; File = 'tools\verify-r5v9-fan-host-payload.ps1'; Line = $verifyDll.Lines[0]; Old = $verifyDll.Olds[0]; New = $newDll.ToUpperInvariant(); Hits = 1 }
$sym += [pscustomobject]@{ Sym = 'verify approvedHostDllSha256 marker'; File = 'tools\verify-r5v9-fan-host-payload.ps1'; Line = $verifyDllMarker.Lines[0]; Old = $verifyDllMarker.Olds[0]; New = $newDll.ToUpperInvariant(); Hits = 1 }
$sym += [pscustomobject]@{ Sym = "verify 'YeManFanHost.exe' pin (expect unchanged)"; File = 'tools\verify-r5v9-fan-host-payload.ps1'; Line = $verifyExe.Lines[0]; Old = $verifyExe.Olds[0]; New = $newExe.ToUpperInvariant(); Hits = 1 }
$sym += [pscustomobject]@{ Sym = 'auth approvedHostDllSha256'; File = 'PowerControl\fan-host\YeManFanHost.authorization.md'; Line = $authDll.Lines[0]; Old = $authDll.Olds[0]; New = $newDll.ToUpperInvariant(); Hits = 1 }
$sym += [pscustomobject]@{ Sym = 'auth approvedHostExeSha256'; File = 'PowerControl\fan-host\YeManFanHost.authorization.md'; Line = $authExe.Lines[0]; Old = $authExe.Olds[0]; New = $newExe.ToUpperInvariant(); Hits = 1 }
$sym += [pscustomobject]@{ Sym = 'auth approvedHcSha256'; File = 'PowerControl\fan-host\YeManFanHost.authorization.md'; Line = $authHc.Lines[0]; Old = $authHc.Olds[0]; New = $newHc.ToUpperInvariant(); Hits = 1 }
$sym += [pscustomobject]@{ Sym = 'native HC pin ($hc[0].sha256)'; File = 'native\main.cpp'; Line = $nativeHc.Lines[0]; Old = $nativeHc.Olds[0]; New = $nativeHc.NewAudit; Hits = 1 }
$sym += [pscustomobject]@{ Sym = 'verify approvedHostExeSha256 marker'; File = 'tools\verify-r5v9-fan-host-payload.ps1'; Line = $verifyExeMarker.Lines[0]; Old = $verifyExeMarker.Olds[0]; New = $newExe.ToUpperInvariant(); Hits = 1 }
$sym += [pscustomobject]@{ Sym = 'verify approvedHcSha256 marker'; File = 'tools\verify-r5v9-fan-host-payload.ps1'; Line = $verifyHcMarker.Lines[0]; Old = $verifyHcMarker.Olds[0]; New = $hcUpper; Hits = 1 }
$sym += [pscustomobject]@{ Sym = 'verify HC comment'; File = 'tools\verify-r5v9-fan-host-payload.ps1'; Line = $verifyHcComment.Lines[0]; Old = $verifyHcComment.Olds[0]; New = $hcShort; Hits = 1 }
$sym += [pscustomobject]@{ Sym = 'realHost ExpectedHcSha256'; File = 'FanLab\real-host\Program.cs'; Line = $realHostPin.Lines[0]; Old = $realHostPin.Olds[0]; New = $newHc; Hits = 1 }
$sym += [pscustomobject]@{ Sym = 'realHost HC comment'; File = 'FanLab\real-host\Program.cs'; Line = $realHostComment.Lines[0]; Old = $realHostComment.Olds[0]; New = $hcShort; Hits = 1 }
$sym += [pscustomobject]@{ Sym = 'lightSetter ExpectedHcSha256'; File = 'FanLab\LightSetter\Program.cs'; Line = $lightSetterPin.Lines[0]; Old = $lightSetterPin.Olds[0]; New = $newHc; Hits = 1 }
$sym += [pscustomobject]@{ Sym = 'lightSetter HC comment'; File = 'FanLab\LightSetter\Program.cs'; Line = $lightSetterComment.Lines[0]; Old = $lightSetterComment.Olds[0]; New = $hcShort; Hits = 1 }
$sym | ForEach-Object { Write-Output ("  {0}|{1}:{2}| {3} -> {4} | hits={5}" -f $_.Sym, $_.File, $_.Line, $_.Old, $_.New, $_.Hits) }
Write-Output ''
Write-Output 'Files to write:'
$pending = @()
$conflict = @()
foreach ($t in $targets) {
  $state = if ($t.CurrentHash -eq $t.NewHash) { 'current' } elseif ($t.CurrentHash -eq $t.PreHash) { 'pending' } else { 'CONFLICT' }
  Write-Output ("  {0} : pre={1} new={2} state={3}" -f $t.Name, $t.PreHash.Substring(0, 12), $t.NewHash.Substring(0, 12), $state)
  if ($state -eq 'pending') { $pending += $t }
  if ($state -eq 'CONFLICT') { $conflict += $t }
}
if ($conflict.Count -gt 0) { throw ("rebaseline conflict: files changed by another writer: " + (($conflict | ForEach-Object { $_.Name }) -join ', ')) }
Write-Output ("install-fan-host-payload.ps1 is NOT rewritten (sha256=$($pre.installPs1Sha256))")
Write-Output ''

if ($pending.Count -eq 0) {
  Write-Output 'ALREADY_CURRENT: no pending writes; nothing to do.'
  Write-Output '==== END PLAN ===='
  return
}

if ($DryRun) {
  Write-Output ("DRY_RUN_PLAN_OK: {0} file(s) would be written." -f $pending.Count)
  Write-Output '==== END PLAN ===='
  return
}

# ---------------------------------------------------------------- step 6: apply + readback
$evidenceDir = if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
  Join-Path $project ('..\..\Temp\coordination\FAN-926R-REPIN-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
} else { [IO.Path]::GetFullPath($EvidenceRoot) }
New-Item -ItemType Directory -Force -Path $evidenceDir | Out-Null

$evidence = [ordered]@{
  at = (Get-Date).ToString('o')
  adjudication = 'FAN-926R-REBASELINE-RECOVERY-AND-MAINLINE-EXPORT-ADJUDICATION-20260926.md section 1.1'
  projectRoot = $project
  hcRuntimeRoot = $hcRoot
  preimage = [ordered]@{
    manifestSha256 = $pre.manifestSha256; hostDllSha256 = $pre.hostDllSha256; hostExeSha256 = $pre.hostExeSha256
    hcDllSha256 = $pre.hcDllSha256; installPs1Sha256 = $pre.installPs1Sha256
  }
  postimage = [ordered]@{
    manifestSha256 = $newManifestSha256; hostDllSha256 = $newDll; hostExeSha256 = $newExe
    hcDllSha256 = $newHc; fileCount = $newFileCount
  }
  buildLog = @($buildLog)
  symbols = @($sym)
  written = @()
}
[IO.File]::WriteAllText((Join-Path $evidenceDir 'preimage.json'), ($evidence | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))

$written = @()
try {
  foreach ($t in $pending) {
    Write-Utf8Atomic $t.Path $t.NewBytes
    $written += [ordered]@{ Name = $t.Name; Path = $t.Path; PreHash = $t.PreHash; NewHash = $t.NewHash; PreBytes = $t.PreBytes }
  }
} catch {
  $applyError = $_
  # Roll back only files whose current content still equals THIS tool's postimage.
  $reverted = @()
  foreach ($w in $written) {
    $now = Get-Sha256 $w.Path
    if ($now -eq $w.NewHash) {
      [IO.File]::WriteAllBytes($w.Path, $w.PreBytes)
      $reverted += $w.Name
    } else {
      Write-Warning ("rollback skipped (concurrent edit preserved): " + $w.Name)
    }
  }
  $evidence.rollback = [ordered]@{ reverted = $reverted; error = $applyError.Exception.Message }
  [IO.File]::WriteAllText((Join-Path $evidenceDir 'preimage.json'), ($evidence | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
  throw ("rebaseline apply failed and was rolled back: " + $applyError.Exception.Message)
}

$evidence.written = @($written | ForEach-Object { $_.Name })
[IO.File]::WriteAllText((Join-Path $evidenceDir 'preimage.json'), ($evidence | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
Write-Output ("APPLIED: {0} file(s) written and read back." -f $written.Count)
Write-Output ("Evidence: $evidenceDir\preimage.json")

# ---------------------------------------------------------------- post-apply verify
& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $verifier
$verifyExit = $LASTEXITCODE
Write-Output ("VERIFY_EXIT=$verifyExit")
if ($verifyExit -ne 0) { throw "rebaseline post-apply verifier failed: exit=$verifyExit" }
Write-Output '==== END PLAN ===='
