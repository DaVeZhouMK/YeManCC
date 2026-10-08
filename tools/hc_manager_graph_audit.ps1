[CmdletBinding()]
param(
  [string]$HcSource = 'deps/handheldcompanion-runtime/source',
  [string]$Output = '../../Build/Validation/HC-Parity/A1-hc-manager-graph-audit.json'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$source = (Resolve-Path (Join-Path $repo $HcSource)).Path
$factoryPath = Join-Path $source 'Managers\ManagerFactory.cs'
$factory = Get-Content -Raw -LiteralPath $factoryPath
$fanTokens = @('SetFanControl','SetFanDuty','ECRam','ACPI','WMI','FanControl','PowerProfile','SuspendWithOS')
$fields = [regex]::Matches($factory, 'public\s+static\s+(?<type>[A-Za-z0-9_<>?,\.]+)\s+(?<name>\w+)\s*;') | ForEach-Object { [ordered]@{ name = $_.Groups['name'].Value; type = $_.Groups['type'].Value } }
$nodes = foreach ($field in $fields) {
  $type = [string]$field.type
  $hits = @(Get-ChildItem -LiteralPath $source -Recurse -File -Filter ($type + '.cs') -ErrorAction SilentlyContinue)
  $files = foreach ($hit in $hits) {
    $text = Get-Content -Raw -LiteralPath $hit.FullName
    [ordered]@{ path = $hit.FullName; directTokens = @($fanTokens | Where-Object { $text.Contains($_) }); staticCtor = [regex]::IsMatch($text, '(?m)static\s+' + [regex]::Escape($type) + '\s*\('); sha256 = (& certutil.exe -hashfile $hit.FullName SHA256 | Select-Object -Skip 1 -First 1).Trim() }
  }
  [ordered]@{ managerField = $field.name; managerType = $type; sourceFiles = @($files) }
}
$inputClasses = foreach ($name in @('ControllerManager','MotionManager','SensorsManager','VirtualManager')) {
  $hits = @(Get-ChildItem -LiteralPath $source -Recurse -File -Filter ($name + '.cs') -ErrorAction SilentlyContinue)
  foreach ($hit in $hits) {
    $text = Get-Content -Raw -LiteralPath $hit.FullName
    [ordered]@{ class = $name; path = $hit.FullName; managerFactoryDependencies = @([regex]::Matches($text, 'ManagerFactory\.(\w+)') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique); directTokens = @($fanTokens | Where-Object { $text.Contains($_) }); sha256 = (& certutil.exe -hashfile $hit.FullName SHA256 | Select-Object -Skip 1 -First 1).Trim() }
  }
}
$directTokenNodes = @($nodes | Where-Object { @($_.sourceFiles | ForEach-Object { $_.directTokens }).Count -gt 0 })
$result = [ordered]@{
  evidenceId = 'A1-HC-MANAGER-GRAPH-AUDIT-20260903'
  status = 'BLOCKED'
  systemMutation = $false
  managerFactory = $factoryPath
  managerCount = @($fields).Count
  nodes = @($nodes)
  inputClassDependencies = @($inputClasses)
  directTokenManagerCount = $directTokenNodes.Count
  conclusion = 'ManagerFactory constructs a broad graph rather than an input-only Host. This audit records per-manager source identity and direct power/fan/ACPI/WMI tokens; absence of a direct token is not treated as proof of transitive isolation. Keep runtime hidden until a dedicated input-only composition is proven.'
  generatedUtc = [DateTime]::UtcNow.ToString('o')
}
$outPath = Join-Path $repo $Output
New-Item -ItemType Directory -Force (Split-Path $outPath) | Out-Null
$result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $outPath -Encoding UTF8
Write-Output "A1 HC MANAGER GRAPH: $($result.status) managers=$($result.managerCount) directTokenManagers=$($result.directTokenManagerCount)"
Write-Output "Evidence: $outPath"
