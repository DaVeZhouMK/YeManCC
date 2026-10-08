<#!
.SYNOPSIS
  Verifies that the Fan Host payload contains every non-framework assembly
  referenced by HC device implementation IL.

.DESCRIPTION
  IDevice.GetCurrent is a shared factory for all HC device classes. A simple
  manifest check cannot prove that its factory closure is complete: a missing
  assembly can otherwise hide the Fan route only after deployment. This script
  reads PE metadata only. It never loads HC, calls WMI, opens a device, or
  touches hardware.
#>
[CmdletBinding()]
param(
  [string]$HcRuntimeRoot = '',
  [Parameter(Mandatory = $true)][string]$PayloadRoot,
  [string]$RuntimeRoot = '',
  [string[]]$ExcludedRuntimeFiles = @()
)

$ErrorActionPreference = 'Stop'
$metadataAssembly = @(
  [IO.Path]::Combine([Runtime.InteropServices.RuntimeEnvironment]::GetRuntimeDirectory(), 'System.Reflection.Metadata.dll'),
  (Join-Path $PSHOME 'System.Reflection.Metadata.dll')
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
# R-publisher（2026-09-11）：本机可能只有 Windows PowerShell 5.1 且无
# GAC/运行时 System.Reflection.Metadata（.NET Core 的 dotnet shared 程序集
# 无法被 .NET Framework 加载）。为保持 fan-host 重基可执行，允许调用方用
# YEMAN_SRM_DLL（单个 netstandard2.0 版 dll）或 YEMAN_SRM_DIR（依赖同目录，
# 例如先放 System.Collections.Immutable.dll 再放 System.Reflection.Metadata.dll）
# 提供。PS 5.1 实测：Immutable(net462) → Metadata(netstandard2.0) 顺序加载
# 0 错、MetadataReader 注册、导出类型 262 个。
if ([string]::IsNullOrWhiteSpace($metadataAssembly) -and -not [string]::IsNullOrWhiteSpace($env:YEMAN_SRM_DLL)) {
  $tester = $env:YEMAN_SRM_DLL
  if (Test-Path -LiteralPath $tester -PathType Leaf) { $metadataAssembly = $tester }
}
if ([string]::IsNullOrWhiteSpace($metadataAssembly) -and -not [string]::IsNullOrWhiteSpace($env:YEMAN_SRM_DIR)) {
  $candidate = Join-Path $env:YEMAN_SRM_DIR 'System.Reflection.Metadata.dll'
  if (Test-Path -LiteralPath $candidate -PathType Leaf) { $metadataAssembly = $candidate }
}
if ([string]::IsNullOrWhiteSpace($metadataAssembly)) {
  throw 'System.Reflection.Metadata is unavailable in the PowerShell runtime. 设置 YEMAN_SRM_DLL=<dll> 或 YEMAN_SRM_DIR=<含依赖目录> 可提供（PS 5.1 实测支持）。'
}
# 依赖优先加载：netstandard2.0 Metadata 需要 System.Collections.Immutable；
# .NET Framework（PS 5.1）无内置，须从 YEMAN_SRM_DIR 预先 Add-Type。
if (-not [string]::IsNullOrWhiteSpace($env:YEMAN_SRM_DIR)) {
  foreach ($dep in @('System.Collections.Immutable.dll', 'System.Runtime.CompilerServices.Unsafe.dll')) {
    $depPath = Join-Path $env:YEMAN_SRM_DIR $dep
    if (Test-Path -LiteralPath $depPath -PathType Leaf) {
      try { Add-Type -Path $depPath -ErrorAction Stop } catch { $null = $_.Exception.Message }
    }
  }
}
try { Add-Type -Path $metadataAssembly }
catch {
  if ($null -eq ('System.Reflection.Metadata.MetadataReader' -as [type])) { throw }
}

function Get-OpCodeMap {
  $map = @{}
  [System.Reflection.Emit.OpCodes].GetFields([Reflection.BindingFlags]'Public,Static') | ForEach-Object {
    $opcode = $_.GetValue($null)
    $map[(([int]$opcode.Value) -band 0xffff)] = $opcode
  }
  return $map
}

function Get-ExternalAssembly($reader, $handle) {
  if ($handle.IsNil) { return $null }
  switch ($handle.Kind.ToString()) {
    'AssemblyReference' {
      return $reader.GetString($reader.GetAssemblyReference([System.Reflection.Metadata.AssemblyReferenceHandle]$handle).Name)
    }
    'TypeReference' {
      $reference = $reader.GetTypeReference([System.Reflection.Metadata.TypeReferenceHandle]$handle)
      return Get-ExternalAssembly $reader $reference.ResolutionScope
    }
    'MemberReference' {
      $reference = $reader.GetMemberReference([System.Reflection.Metadata.MemberReferenceHandle]$handle)
      return Get-ExternalAssembly $reader $reference.Parent
    }
    'MethodSpecification' {
      $specification = $reader.GetMethodSpecification([System.Reflection.Metadata.MethodSpecificationHandle]$handle)
      return Get-ExternalAssembly $reader $specification.Method
    }
    default { return $null }
  }
}

function Get-OperandSize($operandType) {
  switch ($operandType.ToString()) {
    'InlineNone' { return 0 }
    'ShortInlineBrTarget' { return 1 }
    'ShortInlineI' { return 1 }
    'ShortInlineVar' { return 1 }
    'InlineVar' { return 2 }
    'InlineI' { return 4 }
    'InlineBrTarget' { return 4 }
    'InlineField' { return 4 }
    'InlineMethod' { return 4 }
    'InlineSig' { return 4 }
    'InlineString' { return 4 }
    'InlineTok' { return 4 }
    'InlineType' { return 4 }
    'InlineI8' { return 8 }
    'ShortInlineR' { return 4 }
    'InlineR' { return 8 }
    default { throw "Unsupported IL operand type: $operandType" }
  }
}

function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = [IO.File]::OpenRead($Path)
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose() }
  } finally { $sha.Dispose() }
}

if (-not (Test-Path -LiteralPath $PayloadRoot -PathType Container)) { throw "Fan Host payload missing: $PayloadRoot" }
$payloadRootFull = [IO.Path]::GetFullPath($PayloadRoot).TrimEnd('\')

# FanHost may be intentionally thin: the payload manifest can select a shared
# versioned HC runtime one directory outside fan-host. Resolve that manifest
# whenever the caller did not explicitly supply a runtime root, so this audit
# validates the actual deployment contract rather than the obsolete monolithic
# layout.
if ([string]::IsNullOrWhiteSpace($HcRuntimeRoot)) {
  $payloadManifestPath = Join-Path $payloadRootFull 'YeManFanHost.payload.json'
  if (-not (Test-Path -LiteralPath $payloadManifestPath -PathType Leaf)) {
    throw "Fan Host runtime root not supplied and payload manifest missing: $payloadManifestPath"
  }
  $payloadManifest = Get-Content -LiteralPath $payloadManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
  $runtimeManifestRelative = [string]$payloadManifest.runtimeManifest
  if ([string]::IsNullOrWhiteSpace($runtimeManifestRelative) -or [IO.Path]::IsPathRooted($runtimeManifestRelative)) {
    throw 'Fan Host payload runtimeManifest is missing or must be relative.'
  }
  $runtimeManifestPath = [IO.Path]::GetFullPath((Join-Path $payloadRootFull $runtimeManifestRelative))
  if (-not (Test-Path -LiteralPath $runtimeManifestPath -PathType Leaf)) {
    throw "Fan Host shared runtime manifest missing: $runtimeManifestPath"
  }
  $HcRuntimeRoot = Split-Path -Parent $runtimeManifestPath
}
$HcRuntimeRoot = [IO.Path]::GetFullPath($HcRuntimeRoot).TrimEnd('\')
$hcAssembly = Join-Path $HcRuntimeRoot 'HandheldCompanion.dll'
if (-not (Test-Path -LiteralPath $hcAssembly -PathType Leaf)) { throw "HC assembly missing: $hcAssembly" }
$runtimeRoot = if ([string]::IsNullOrWhiteSpace($RuntimeRoot)) { $HcRuntimeRoot } else { [IO.Path]::GetFullPath($RuntimeRoot).TrimEnd('\') }
if (-not (Test-Path -LiteralPath $runtimeRoot -PathType Container)) { throw "HC runtime payload missing: $runtimeRoot" }
$excludedRuntime = @{}
$normalizedExcludedRuntimeFiles = @($ExcludedRuntimeFiles | ForEach-Object {
  ([string]$_ -split ',') | ForEach-Object { $_.Trim() }
} | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
foreach ($name in $normalizedExcludedRuntimeFiles) {
  if ([string]::IsNullOrWhiteSpace($name) -or [IO.Path]::GetFileName($name) -ne $name) {
    throw "Excluded HC runtime file name is unsafe: $name"
  }
  if (-not (Test-Path -LiteralPath (Join-Path $HcRuntimeRoot $name) -PathType Leaf)) {
    # The locked HC candidate may be pre-pruned; an absent excluded assembly
    # is already compliant and must not block payload generation.
    continue
  }
  $excludedRuntime[$name] = $true
}

$payloadAssemblies = @{}
Get-ChildItem -LiteralPath $PayloadRoot -File -Filter '*.dll' | ForEach-Object { $payloadAssemblies[$_.BaseName] = $true }
if ($runtimeRoot -ne $PayloadRoot) {
  Get-ChildItem -LiteralPath $runtimeRoot -File -Filter '*.dll' | ForEach-Object { $payloadAssemblies[$_.BaseName] = $true }
}
$frameworkPattern = '^(?:System(?:\.|$)|Microsoft(?:\.|$)|netstandard$|WindowsBase$|PresentationCore$|PresentationFramework$|Accessibility$|UIAutomation(?:\.|$))'

# Device IL identifies only the factory call graph. HC's own startup also
# creates ManagerFactory, which creates GPU/library managers before any route
# is selected. Validate the complete Windows closure declared by the frozen HC
# deps manifest, plus HC's native device helpers that are copied outside it.
$hcDepsPath = Join-Path $HcRuntimeRoot 'HandheldCompanion.deps.json'
if (-not (Test-Path -LiteralPath $hcDepsPath -PathType Leaf)) { throw "HC deps manifest missing: $hcDepsPath" }
if (-not (Test-Path -LiteralPath (Join-Path $runtimeRoot 'HandheldCompanion.deps.json') -PathType Leaf)) {
  throw 'HC runtime payload omits HandheldCompanion.deps.json'
}
$hcDeps = Get-Content -LiteralPath $hcDepsPath -Raw -Encoding UTF8 | ConvertFrom-Json
$targetProperties = @($hcDeps.targets.PSObject.Properties)
if ($targetProperties.Count -eq 0) { throw 'HC deps manifest has no target graph' }
$targetProperty = $targetProperties | Where-Object { $_.Name -match '(?i)/win-x64$' } | Select-Object -First 1
if ($null -eq $targetProperty) { $targetProperty = $targetProperties | Select-Object -First 1 }
$target = $targetProperty.Value
$runtimeClosure = @{}
foreach ($library in $target.PSObject.Properties.Value) {
  foreach ($asset in @($library.runtime.PSObject.Properties.Name)) {
    $name = [IO.Path]::GetFileName([string]$asset)
    $source = Join-Path $HcRuntimeRoot $name
    if (-not $excludedRuntime.ContainsKey($name) -and $name -match '\.dll$' -and (Test-Path -LiteralPath $source -PathType Leaf)) { $runtimeClosure[$name] = $source }
  }
  foreach ($asset in @($library.runtimeTargets.PSObject.Properties.Name)) {
    $relative = ([string]$asset).Replace('/', '\')
    $source = Join-Path $HcRuntimeRoot $relative
    if (-not $excludedRuntime.ContainsKey([IO.Path]::GetFileName($relative)) -and $relative -match '(?i)^runtimes\\win(?:-|\\)' -and (Test-Path -LiteralPath $source -PathType Leaf)) {
      $runtimeClosure[[IO.Path]::GetFileName($relative)] = $source
    }
  }
}
$unavailableReferenceAssets = @()
foreach ($name in @('GamepadMotion.dll', 'hidapi.dll', 'IGCL_Wrapper.dll', 'JoyShockLibrary.dll', 'libVIIPER.dll', 'SapientiaUsb.dll', 'SDL3.dll', 'UEFIVaribleDll.dll', 'Xinput1_4.dll')) {
  $source = Join-Path $HcRuntimeRoot $name
  if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    $unavailableReferenceAssets += $name
    continue
  }
  if ($excludedRuntime.ContainsKey($name)) { continue }
  $runtimeClosure[$name] = $source
}
$runtimeMissing = @()
$runtimeMismatched = @()
foreach ($entry in $runtimeClosure.GetEnumerator()) {
  $target = Join-Path $runtimeRoot $entry.Key
  if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { $runtimeMissing += $entry.Key; continue }
  if ((Get-Sha256 $target) -ne (Get-Sha256 $entry.Value)) { $runtimeMismatched += $entry.Key }
}
if ($runtimeMissing.Count -gt 0 -or $runtimeMismatched.Count -gt 0) {
  throw "Fan Host HC runtime closure is incomplete: missing=$($runtimeMissing -join ', '); hashMismatch=$($runtimeMismatched -join ', ')"
}

$stream = [IO.File]::OpenRead($hcAssembly)
try {
  $pe = [System.Reflection.PortableExecutable.PEReader]::new($stream)
  try {
    $reader = [System.Reflection.Metadata.PEReaderExtensions]::GetMetadataReader($pe)
    $opcodes = Get-OpCodeMap
    $deviceTypes = @($reader.TypeDefinitions | Where-Object {
      $type = $reader.GetTypeDefinition($_)
      $namespace = $reader.GetString($type.Namespace)
      $namespace -eq 'HandheldCompanion.Devices' -or $namespace.StartsWith('HandheldCompanion.Devices.')
    })
    $edges = [Collections.Generic.List[object]]::new()

    foreach ($typeHandle in $deviceTypes) {
      $type = $reader.GetTypeDefinition($typeHandle)
      $typeName = $reader.GetString($type.Namespace) + '.' + $reader.GetString($type.Name)
      foreach ($methodHandle in $type.GetMethods()) {
        $method = $reader.GetMethodDefinition($methodHandle)
        if ($method.RelativeVirtualAddress -eq 0) { continue }
        $body = [System.Reflection.Metadata.PEReaderExtensions]::GetMethodBody($pe, $method.RelativeVirtualAddress)
        $bytes = $body.GetILBytes()
        $offset = 0
        while ($offset -lt $bytes.Length) {
          $first = [int]$bytes[$offset]
          $offset++
          $opcodeKey = if ($first -eq 0xfe) {
            $combined = 0xfe00 -bor [int]$bytes[$offset]
            $offset++
            $combined
          } else { $first }
          $opcode = $opcodes[$opcodeKey]
          if ($null -eq $opcode) { throw "Unknown IL opcode in $typeName" }
          $operandKind = $opcode.OperandType.ToString()
          if ($operandKind -eq 'InlineSwitch') {
            $count = [BitConverter]::ToInt32($bytes, $offset)
            $offset += 4 + (4 * $count)
            continue
          }
          $operandSize = Get-OperandSize $opcode.OperandType
          if ($operandKind -in @('InlineField', 'InlineMethod', 'InlineTok', 'InlineType')) {
            $token = [BitConverter]::ToInt32($bytes, $offset)
            $dependency = Get-ExternalAssembly $reader ([System.Reflection.Metadata.Ecma335.MetadataTokens]::EntityHandle($token))
            if (-not [string]::IsNullOrWhiteSpace($dependency)) {
              $edges.Add([pscustomobject]@{ DeviceType = $typeName; Dependency = $dependency })
            }
          }
          $offset += $operandSize
        }
      }
    }

    $missing = @($edges | Where-Object {
      $_.Dependency -notmatch $frameworkPattern -and -not $payloadAssemblies.ContainsKey($_.Dependency)
    } | Group-Object Dependency | Sort-Object Name | ForEach-Object {
      "$($_.Name) <- $(@($_.Group.DeviceType | Sort-Object -Unique) -join ', ')"
    })
    if ($missing.Count -gt 0) {
      throw "Fan Host HC device closure is incomplete: $($missing -join '; ')"
    }
    if ($unavailableReferenceAssets.Count -gt 0) {
      Write-Output "fan HC runtime/device closure self-test: needs-investigation (selfConsistentRuntimeFiles=$($runtimeClosure.Count), deviceTypes=$($deviceTypes.Count), unavailableReferenceAssets=$($unavailableReferenceAssets -join ', '), hardwareWrites=false)"
    } else {
      Write-Output "fan HC runtime/device closure self-test: PASS (runtimeFiles=$($runtimeClosure.Count), deviceTypes=$($deviceTypes.Count), hardwareWrites=false)"
    }
  } finally { $pe.Dispose() }
} finally { $stream.Dispose() }
