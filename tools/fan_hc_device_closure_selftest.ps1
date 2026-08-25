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
  [Parameter(Mandatory = $true)][string]$HcRuntimeRoot,
  [Parameter(Mandatory = $true)][string]$PayloadRoot
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Reflection.Metadata

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

$hcAssembly = Join-Path $HcRuntimeRoot 'HandheldCompanion.dll'
if (-not (Test-Path -LiteralPath $hcAssembly -PathType Leaf)) { throw "HC assembly missing: $hcAssembly" }
if (-not (Test-Path -LiteralPath $PayloadRoot -PathType Container)) { throw "Fan Host payload missing: $PayloadRoot" }

$payloadAssemblies = @{}
Get-ChildItem -LiteralPath $PayloadRoot -File -Filter '*.dll' | ForEach-Object { $payloadAssemblies[$_.BaseName] = $true }
$frameworkPattern = '^(?:System(?:\.|$)|Microsoft(?:\.|$)|netstandard$|WindowsBase$|PresentationCore$|PresentationFramework$|Accessibility$|UIAutomation(?:\.|$))'

# Device IL identifies only the factory call graph. HC's own startup also
# creates ManagerFactory, which creates GPU/library managers before any route
# is selected. Validate the complete Windows closure declared by the frozen HC
# deps manifest, plus HC's native device helpers that are copied outside it.
$hcDepsPath = Join-Path $HcRuntimeRoot 'HandheldCompanion.deps.json'
if (-not (Test-Path -LiteralPath $hcDepsPath -PathType Leaf)) { throw "HC deps manifest missing: $hcDepsPath" }
if (-not (Test-Path -LiteralPath (Join-Path $PayloadRoot 'HandheldCompanion.deps.json') -PathType Leaf)) {
  throw 'Fan Host payload omits HandheldCompanion.deps.json'
}
$hcDeps = Get-Content -LiteralPath $hcDepsPath -Raw -Encoding UTF8 | ConvertFrom-Json
$targets = @($hcDeps.targets.PSObject.Properties.Value)
if ($targets.Count -ne 1) { throw 'HC deps manifest has an unexpected target graph' }
$runtimeClosure = @{}
foreach ($library in $targets[0].PSObject.Properties.Value) {
  foreach ($asset in @($library.runtime.PSObject.Properties.Name)) {
    $name = [IO.Path]::GetFileName([string]$asset)
    $source = Join-Path $HcRuntimeRoot $name
    if ($name -match '\.dll$' -and (Test-Path -LiteralPath $source -PathType Leaf)) { $runtimeClosure[$name] = $source }
  }
  foreach ($asset in @($library.runtimeTargets.PSObject.Properties.Name)) {
    $relative = ([string]$asset).Replace('/', '\')
    $source = Join-Path $HcRuntimeRoot $relative
    if ($relative -match '(?i)^runtimes\\win(?:-|\\)' -and (Test-Path -LiteralPath $source -PathType Leaf)) {
      $runtimeClosure[[IO.Path]::GetFileName($relative)] = $source
    }
  }
}
foreach ($name in @('GamepadMotion.dll', 'hidapi.dll', 'IGCL_Wrapper.dll', 'JoyShockLibrary.dll', 'libVIIPER.dll', 'SapientiaUsb.dll', 'SDL3.dll', 'UEFIVaribleDll.dll', 'Xinput1_4.dll')) {
  $source = Join-Path $HcRuntimeRoot $name
  if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "HC native device dependency missing: $name" }
  $runtimeClosure[$name] = $source
}
$runtimeMissing = @()
$runtimeMismatched = @()
foreach ($entry in $runtimeClosure.GetEnumerator()) {
  $target = Join-Path $PayloadRoot $entry.Key
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
    Write-Output "fan HC runtime/device closure self-test: PASS (runtimeFiles=$($runtimeClosure.Count), deviceTypes=$($deviceTypes.Count), hardwareWrites=false)"
  } finally { $pe.Dispose() }
} finally { $stream.Dispose() }
