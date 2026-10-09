$ErrorActionPreference = 'Stop'

# Inventory only. This audit never starts services, loads DLLs, changes drivers,
# writes registry values, or makes any runtime-admission decision.
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$validation = Join-Path $repo 'Mainline\Build\Validation\HC-Parity'

function Get-FileMetadata([string]$path) {
  $item = Get-Item -LiteralPath $path -Force
  $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($path)
  $stream = [IO.File]::OpenRead($path)
  $sha = [Security.Cryptography.SHA256]::Create()
  try { $hash = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '') } finally { $sha.Dispose(); $stream.Dispose() }
  return [ordered]@{
    path = $path
    length = $item.Length
    sha256 = $hash
    fileVersion = $version.FileVersion
    productVersion = $version.ProductVersion
  }
}

function Find-UninstallEntries([string]$pattern) {
  $roots = @(
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall',
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall',
    'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall'
  )
  return @($roots | Where-Object { Test-Path -LiteralPath $_ } | ForEach-Object {
    Get-ChildItem -LiteralPath $_ -ErrorAction SilentlyContinue | ForEach-Object {
      $p = Get-ItemProperty -LiteralPath $_.PSPath -ErrorAction SilentlyContinue
      if ($p.DisplayName -match $pattern) {
        [ordered]@{ registryKey=$_.PSPath; displayName=$p.DisplayName; displayVersion=$p.DisplayVersion; installLocation=$p.InstallLocation; uninstallString=$p.UninstallString }
      }
    }
  })
}

function Find-Files([string[]]$roots, [string]$pattern) {
  return @($roots | Where-Object { Test-Path -LiteralPath $_ -PathType Container } | ForEach-Object {
    Get-ChildItem -LiteralPath $_ -File -Recurse -ErrorAction SilentlyContinue |
      Where-Object { $_.Name -match $pattern } |
      Select-Object -First 20 |
      ForEach-Object { Get-FileMetadata $_.FullName }
  })
}

$programRoots = @('C:\Program Files', 'C:\Program Files (x86)')
$rewasd = [ordered]@{
  product = 'reWASD'
  uninstallEntries = [object[]]@(Find-UninstallEntries 'reWASD')
  files = [object[]]@(Find-Files $programRoots '(?i)^rewasd.*\.(exe|dll)$')
}
$xinputPlus = [ordered]@{
  product = 'XInputPlus'
  uninstallEntries = [object[]]@(Find-UninstallEntries 'XInputPlus')
  files = [object[]]@(Find-Files $programRoots '(?i)^XInputPlus.*\.(exe|dll)$')
}
$hcRoots = @(
  (Join-Path $project 'deps\handheldcompanion-runtime'),
  (Join-Path $project 'PowerControl\handheldcompanion-runtime'),
  (Join-Path $project 'FanLab')
)
$hcVigem = [ordered]@{
  product = 'HC ViGEm candidate graph'
  uninstallEntries = [object[]]@(Find-UninstallEntries 'ViGEm|Nefarius')
  files = [object[]]@(Find-Files $hcRoots '(?i)(ViGEm|Nefarius\.ViGEm).*\.(dll|cs)$')
}
$driverServices = [object[]]@(Get-CimInstance -ClassName Win32_SystemDriver -ErrorAction SilentlyContinue |
  Where-Object { $_.Name -match 'ViGEm|reWASD|XInputPlus' -or $_.DisplayName -match 'ViGEm|reWASD|XInputPlus' } |
  ForEach-Object { [ordered]@{ name=$_.Name; displayName=$_.DisplayName; state=$_.State; pathName=$_.PathName } })

$products = @($rewasd, $hcVigem, $xinputPlus)
$located = @($products | Where-Object { $_.uninstallEntries.Count -gt 0 -or $_.files.Count -gt 0 }).Count -gt 0
$result = [ordered]@{
  evidenceId = 'A1-THIRD-PARTY-INPUT-ISOLATION-READONLY-20260903'
  status = if ($located -or $driverServices.Count -gt 0) { 'LOCATED_UNVERIFIED' } else { 'NOT_LOCATED' }
  systemMutation = $false
  runtimeAdmission = 'UNENCLOSED'
  gameConsumerIsolationProven = $false
  recoveryProven = $false
  hidHideIsNotXInputBlocker = $true
  products = $products
  matchingDriverServices = $driverServices
  conclusion = 'Read-only inventory only. Locating reWASD, HC ViGEm, XInputPlus, or a related driver does not prove per-game consumer isolation, owner handoff, held-state cleanup, or recovery. HidHide is not treated as an XInput game-consumer blocker. No third-party mechanism is admitted to YMCC runtime.'
  generatedUtc = [DateTime]::UtcNow.ToString('o')
}

New-Item -ItemType Directory -Path $validation -Force | Out-Null
$out = Join-Path $validation 'A1-third-party-input-isolation-readonly-20260903.json'
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $out -Encoding UTF8
Write-Output "A1 THIRD-PARTY INPUT ISOLATION READONLY: $($result.status)"
Write-Output "Evidence: $out"
